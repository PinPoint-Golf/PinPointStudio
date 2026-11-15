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

// AppSettings' two rule-data settings as a view over RuleData (refactor step 3.2; D5: DEC-170; SET-06,
// SET-15). Apart from app_settings.cpp so that a target which never binds (a test with its own AppSettings)
// links no RuleData.

#include "app_settings.h"

#include "../../DomainModel/rule_data.h"

void AppSettings::bindRuleData(pinpoint::RuleData *ruleData)
{
    if (!ruleData) return;
    m_ruleSetGradePolicy = [ruleData](const QString &v) { ruleData->setGradePolicy(v); };
    m_ruleSetNormSetsOff = [ruleData](const QStringList &v) { ruleData->setDisabledNormSets(v); };

    // Take the values in force, now and on every EVT-12 (which a SET-06 / SET-15 change publishes), and
    // raise the NOTIFY only for the one that moved.
    const auto pull = [this, ruleData] {
        const std::shared_ptr<const pinpoint::RuleDataSnapshot> s = ruleData->snapshot();
        if (s->gradePolicy != m_diagnosticsGradePolicy) {
            m_diagnosticsGradePolicy = s->gradePolicy;
            emit diagnosticsGradePolicyChanged();
        }
        if (s->disabledNormSets != m_diagnosticsNormSetsOff) {
            m_diagnosticsNormSetsOff = s->disabledNormSets;
            emit diagnosticsNormSetsOffChanged();
        }
    };
    pull();
    QObject::connect(ruleData, &pinpoint::RuleData::normsChanged, this, pull);
}
