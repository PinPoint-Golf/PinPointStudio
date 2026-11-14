/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "consistency_env.h"

#include "../../Coaching/swing_areas_pack.h"        // metricAreasForPack
#include "../../Diagnostics/norms/context_tree.h"   // contextIdForClub
#include "../../Diagnostics/norms/norm_provider.h"
#include "../../Diagnostics/packs/pack_provider.h"
#include "../../Metrics/metric_catalogue.h"
#include "../../Core/pp_debug.h"                    // ppError
#include "../typeset/box_stats.h"                   // quantile7

#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QString>
#include <QThread>

#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

namespace pinpoint::charts {

namespace {

namespace pa = pinpoint::analysis;

double consistencyHalfSigma(const pa::Norm &n) { return 0.5 * (n.sigmaLo + n.sigmaHi); }

// The pack half of what the env reads: built ONCE and never changed after, so its readers take no lock.
// It does not follow a Diagnostic Model save (pack edits reach the app at the next launch, audit B-02).
class ConsistencyPackFacts {
public:
    ConsistencyPackFacts() : m_provider(pa::makeCharacteristicPackProvider())
    {
        if (m_provider) {
            m_areas = pa::metricAreasForPack(m_provider->pack());
            m_afterBall = pa::afterBallKeysForPack(m_provider->pack());
        }
        // The manifest alone, not makeMetricCatalogue(): only the descriptors are needed here, and that
        // call also writes a process-wide labeller, which a worker has no business doing.
        pa::MetricCatalogue catalogue;
        pa::installMetricManifest(catalogue);
        for (const pa::MetricDescriptor *d : catalogue.all())
            if (!d->consistencyArea.isEmpty()) m_areas.insert(d->key, d->consistencyArea);
    }

    const pa::CharacteristicPack *pack() const { return m_provider ? &m_provider->pack() : nullptr; }
    QString areaOf(const QString &key) const { return m_areas.value(key); }
    bool afterBall(const QString &key) const { return m_afterBall.contains(key); }

private:
    std::unique_ptr<pa::ICharacteristicPackProvider> m_provider;
    QHash<QString, QString>                          m_areas;       // key -> area token
    QSet<QString>                                    m_afterBall;   // keys graded after the ball has gone
};

const ConsistencyPackFacts &consistencyPackFacts()
{
    static const ConsistencyPackFacts facts;     // C++ builds a function-local static once, under its own lock
    return facts;
}

// The norm half: ONE norm snapshot (norm_provider.h) and the sigma memo worked out over it. Immutable but
// for the memo, so an env holding one grades against one norm set however long it lives; a corridor
// saved in the Diagnostic Model makes a NEW one (consistencyNorms), never changes this.
class ConsistencyNorms {
public:
    ConsistencyNorms(const ConsistencyPackFacts *facts, std::shared_ptr<const pa::INormProvider> norms)
        : m_facts(facts), m_norms(std::move(norms))
    {
    }

    double normSigma(const QString &key, int phase, const QString &club) const
    {
        const QString context = pa::contextIdForClub(club);
        const QString memoKey = key + QLatin1Char('|') + QString::number(phase) + QLatin1Char('|') + context;
        {
            QMutexLocker lock(&m_memoLock);
            const auto it = m_memo.constFind(memoKey);
            if (it != m_memo.constEnd()) return it.value();
        }
        const double sigma = lookUpNormSigma(key, phase, context);      // two threads may both work it out
        QMutexLocker lock(&m_memoLock);
        m_memo.insert(memoKey, sigma);
        return sigma;
    }

private:
    double lookUpNormSigma(const QString &key, int phase, const QString &context) const
    {
        const pa::CharacteristicPack *packp = m_facts->pack();
        if (!packp || !m_norms) return kNaN;
        const pa::CharacteristicPack &pack = *packp;

        // Every measure at the phase, not the winner: the preferred one may carry no norm
        // (m_leadWristAtTop at P4) while the one beside it does.
        for (const pa::Measure *m : pa::measuresForMetricAtPhase(pack, key, static_cast<pa::Phase>(phase))) {
            const pa::NormResolution res = m_norms->resolve(m->id, context);
            if (res.found()) return consistencyHalfSigma(*res.norm);
        }

        // Nothing there (a span's end can fall between the measures, and a peak reads at no phase): the
        // key's typical sigma. resolve walks up to `any` itself, so the second pass answers only a
        // context the tree does not know.
        for (const QString &ctx : { context, QStringLiteral("any") }) {
            std::vector<double> sigmas;
            for (const pa::Measure &m : pack.measures) {
                if (!pa::measureKeyLadder(m).contains(key)) continue;
                const pa::NormResolution res = m_norms->resolve(m.id, ctx);
                if (res.found()) sigmas.push_back(consistencyHalfSigma(*res.norm));
            }
            if (!sigmas.empty()) return pinpoint::box::quantile7(std::move(sigmas), 0.5);
        }
        return kNaN;
    }

    const ConsistencyPackFacts              *m_facts;     // process lifetime
    std::shared_ptr<const pa::INormProvider> m_norms;

    mutable QMutex                  m_memoLock;
    mutable QHash<QString, double>  m_memo;                         // "key|phase|context" -> sigma
};

// The current norm half, made again when the shared norm set has moved since the last one was made
// (a Diagnostic Model save resets it). An env already handed out keeps the half it was given.
std::shared_ptr<const ConsistencyNorms> consistencyNorms(const ConsistencyPackFacts *facts)
{
    static QMutex                                  lock;
    static std::shared_ptr<const ConsistencyNorms> current;
    static quint64                                 builtAt = 0;
    QMutexLocker lk(&lock);
    if (!current || builtAt != pa::sharedNormProviderGeneration()) {
        std::shared_ptr<const pa::INormProvider> norms = pa::sharedNormProvider(&builtAt);
        current = std::make_shared<const ConsistencyNorms>(facts, std::move(norms));
    }
    return current;
}

} // namespace

ConsistencyEnv consistencyEnvFromPacks()
{
    // The header's rule. The check has to come before the build it guards.
    static std::atomic<bool> taken{ false };
    if (!taken.exchange(true) && !QThread::isMainThread()) {
        ppError() << "Consistency: the first consistencyEnvFromPacks() came from a worker; take it on the GUI thread";
        Q_ASSERT_X(false, "consistencyEnvFromPacks", "the first call must be on the GUI thread");
    }
    const ConsistencyPackFacts *facts = &consistencyPackFacts();
    const std::shared_ptr<const ConsistencyNorms> norms = consistencyNorms(facts);
    ConsistencyEnv env;
    env.areaOf = [facts](const QString &key) { return facts->areaOf(key); };
    env.afterBall = [facts](const QString &key) { return facts->afterBall(key); };
    env.normSigma = [norms](const QString &key, int phase, const QString &club) {
        return norms->normSigma(key, phase, club);
    };
    return env;
}

} // namespace pinpoint::charts
