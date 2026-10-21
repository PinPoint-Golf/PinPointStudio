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

#include "pdf_document_writer.h"

#include <QBuffer>
#include <QColorSpace>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QImageWriter>
#include <QJSValue>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPixmap>
#include <QSaveFile>
#include <QUrl>
#include <QUuid>

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <vector>

namespace {

// QML hands paths over as strings that are sometimes "file:///…" URLs.
QString localPath(const QString &pathOrUrl)
{
    if (pathOrUrl.startsWith(QLatin1String("file:"), Qt::CaseInsensitive))
        return QUrl(pathOrUrl).toLocalFile();
    return pathOrUrl;
}

constexpr int kMinDpi = 72;
constexpr int kMaxDpi = 1200;

} // namespace

// ── "auto": PinPoint's own PDF/A-1b serializer ────────────────────────────────────────────────
//
// Why not QPdfWriter: measured against Qt 6.11.1 (build/report-build/encoding/qtcheck), QPdfEngine
// writes every image as DeviceRGB — an Indexed8 QImage is expanded to 24-bit RGB before Flate, so
// a palette buys nothing — and its JPEG quality is hard-coded at 94 (a still came out at exactly
// QImageWriter's q94 byte count). LosslessImageRendering only chooses between those two. A report
// page is ~8.7 Mpx at 300 dpi; as RGB Flate it is 0.3–1.3 MB, as an 8-bit palette 0.15–0.4 MB.
// So "auto" writes the file itself. The pages are images only (no fonts, no vectors), so a
// PDF/A-1b file is small to write by hand: a header with a binary comment, Info + an unfiltered
// XMP packet that say the same things, Qt's own sRGB ICC profile as the output intent, one
// image XObject (or a few) and a one-line content stream per page, an xref and a trailer /ID.
namespace pdf_detail {

constexpr int kFlateLevel = 9;          // ~0.3 s a page; level 6 is 2–4 % bigger
constexpr int kPhotoJpegQuality = 90;   // the stills only; see build/report-build/encoding/
constexpr int kPhotoBlock = 16;         // photo finding works on 16 × 16 px blocks
constexpr int kSeamRim = 3;             // px of real (quantised) photo kept under a tile's edge
constexpr int kSolidPixels = 1500;      // a colour this common is ink or paper: kept exactly
constexpr int kMaxSolid = 48;
constexpr int kBadError = 24;           // a channel off by more than this is a visible change…
constexpr double kBadShare = 0.002;     // …and on more than 0.2 % of the page, a palette is wrong

QByteArray flate(const QByteArray &raw)
{
    // qCompress prefixes the uncompressed length (4 bytes, big-endian); the rest is the zlib
    // stream FlateDecode reads.
    QByteArray z = qCompress(raw, kFlateLevel);
    z.remove(0, 4);
    return z;
}

QByteArray num(double v)
{
    QByteArray b = QByteArray::number(v, 'f', 4);
    while (b.contains('.') && (b.endsWith('0') || b.endsWith('.')))
        b.chop(1);
    return b;
}

// A PDF text string: UTF-16BE with a byte-order mark, written as hex so nothing needs escaping.
QByteArray textString(const QString &s)
{
    QByteArray utf16("\xFE\xFF", 2);
    for (QChar c : s) {
        utf16.append(char(c.unicode() >> 8));
        utf16.append(char(c.unicode() & 0xff));
    }
    return '<' + utf16.toHex().toUpper() + '>';
}

// One image XObject on a page and where it sits, in the page image's own pixels.
struct PdfImage {
    QByteArray dict;   // the dictionary's entries after /Subtype /Image, without /Length
    QByteArray data;
    QRect px;
};

struct EncodedPage {
    QVector<PdfImage> images;   // drawn in order: the page, then any photographs over it
    QVariantMap info;
};

// Exact colour → count (and, once a palette exists, → index). Open addressing; a page has a few
// thousand colours, a page of photographs a few hundred thousand.
class ColourTable
{
public:
    struct Slot { quint32 key = 0; quint32 count = 0; int index = -1; };

    ColourTable() { rehash(14); }
    void add(QRgb c, quint32 n)
    {
        if ((m_size + 1) * 2 > m_slots.size())
            rehash(m_bits + 1);
        Slot &s = m_slots[probe(c | 0xFF000000u)];
        if (!s.key) {
            s.key = c | 0xFF000000u;
            ++m_size;
        }
        s.count += n;
    }
    const Slot *find(QRgb c) const
    {
        const Slot &s = m_slots[probe(c | 0xFF000000u)];
        return s.key ? &s : nullptr;
    }
    std::vector<Slot> &all() { return m_slots; }
    int size() const { return int(m_size); }

private:
    size_t probe(quint32 key) const
    {
        const size_t mask = m_slots.size() - 1;
        size_t i = quint32(key * 2654435761u) >> (32 - m_bits);
        while (m_slots[i].key && m_slots[i].key != key)
            i = (i + 1) & mask;
        return i;
    }
    void rehash(int bits)
    {
        std::vector<Slot> old;
        old.swap(m_slots);
        m_bits = bits;
        m_slots.assign(size_t(1) << bits, Slot());
        for (const Slot &s : old)
            if (s.key)
                m_slots[probe(s.key)] = s;
    }
    std::vector<Slot> m_slots;
    size_t m_size = 0;
    int m_bits = 0;
};

// The number of colour components in a baseline/progressive JPEG (its SOF marker), 0 if unread.
int jpegComponents(const QByteArray &jpeg)
{
    const auto *d = reinterpret_cast<const uchar *>(jpeg.constData());
    const qsizetype n = jpeg.size();
    qsizetype i = 2;
    while (i + 9 < n) {
        if (d[i] != 0xFF)
            return 0;
        const uchar m = d[i + 1];
        const int len = (d[i + 2] << 8) | d[i + 3];
        if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC)
            return d[i + 9];
        i += 2 + len;
    }
    return 0;
}

// A photograph as an image of its own: JPEG at kPhotoJpegQuality (Flate RGB if this Qt has no
// JPEG writer, so a missing plugin costs size, never the picture).
PdfImage photoImage(const QImage &page, const QRect &r)
{
    PdfImage im;
    im.px = r;
    const QImage tile = page.copy(r).convertToFormat(QImage::Format_RGB888);
    const QByteArray size = "/Width " + QByteArray::number(r.width()) + " /Height "
                            + QByteArray::number(r.height()) + " /BitsPerComponent 8";
    QByteArray jpeg;
    {
        QBuffer buf(&jpeg);
        buf.open(QIODevice::WriteOnly);
        QImageWriter w(&buf, "jpeg");
        w.setQuality(kPhotoJpegQuality);
        w.setOptimizedWrite(true);
        if (!w.write(tile))
            jpeg.clear();
    }
    const int comps = jpeg.isEmpty() ? 0 : jpegComponents(jpeg);
    if (comps == 3 || comps == 1) {
        im.dict = size + (comps == 3 ? " /ColorSpace /DeviceRGB" : " /ColorSpace /DeviceGray")
                  + " /Filter /DCTDecode";
        im.data = jpeg;
        return im;
    }
    QByteArray rgb;
    rgb.reserve(qsizetype(tile.width()) * tile.height() * 3);
    for (int y = 0; y < tile.height(); ++y)
        rgb.append(reinterpret_cast<const char *>(tile.constScanLine(y)), qsizetype(tile.width()) * 3);
    im.dict = size + " /ColorSpace /DeviceRGB /Filter /FlateDecode";
    im.data = flate(rgb);
    return im;
}

// Where the photographs are on a page. A photograph is the one thing on a report page that is
// busy everywhere: in a 16 px block of it nearly every pixel differs and no colour is common,
// where a block of text — however dense its anti-aliasing (the diagnostics print reaches 176
// colours in a block) — is still mostly paper. Busy blocks are grouped, a group too small or
// too sparse to be a picture is dropped, and each group's box is grown by a block and trimmed
// back to where the plain paper round it starts.
QVector<QRect> findPhotos(const QImage &img)
{
    const int B = kPhotoBlock;
    const int W = img.width() / B, H = img.height() / B;
    if (W < 2 || H < 2)
        return {};
    std::vector<uchar> busy(size_t(W) * H, 0);
    quint32 keys[512];
    quint16 counts[512];
    for (int by = 0; by < H; ++by) {
        for (int bx = 0; bx < W; ++bx) {
            std::fill(std::begin(keys), std::end(keys), 0u);
            int distinct = 0, mode = 0;
            for (int y = 0; y < B; ++y) {
                const auto *line = reinterpret_cast<const QRgb *>(img.constScanLine(by * B + y)) + bx * B;
                for (int x = 0; x < B; ++x) {
                    const quint32 k = line[x] | 0xFF000000u;
                    quint32 i = (k * 2654435761u) >> 23;
                    while (keys[i] && keys[i] != k)
                        i = (i + 1) & 511;
                    if (!keys[i]) {
                        keys[i] = k;
                        counts[i] = 0;
                        ++distinct;
                    }
                    mode = std::max(mode, int(++counts[i]));
                }
            }
            busy[size_t(by) * W + bx] = distinct > 32 && mode * 100 < B * B * 15;
        }
    }

    // The paper: the commonest colour of a sparse sample of the page.
    QRgb paper = 0xFFFFFFFF;
    {
        ColourTable sample;
        for (int y = 0; y < img.height(); y += 7) {
            const auto *l = reinterpret_cast<const QRgb *>(img.constScanLine(y));
            for (int x = 0; x < img.width(); x += 7)
                sample.add(l[x], 1);
        }
        quint32 most = 0;
        for (const ColourTable::Slot &sl : sample.all())
            if (sl.key && sl.count > most) {
                most = sl.count;
                paper = sl.key;
            }
    }
    // A row (column) that is at least 90 % paper is page, not picture. A box is grown outward to
    // the first such row — a still's flat dark backdrop is not busy, but it is not paper either —
    // and trimmed inward to it.
    auto rowIs = [&](QRgb paper, int y, int x0, int x1) {
        const auto *l = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        int same = 0;
        for (int x = x0; x <= x1; ++x)
            same += l[x] == paper;
        return same * 10 >= (x1 - x0 + 1) * 9;
    };
    auto colIs = [&](QRgb paper, int x, int y0, int y1) {
        int same = 0;
        for (int y = y0; y <= y1; ++y)
            same += reinterpret_cast<const QRgb *>(img.constScanLine(y))[x] == paper;
        return same * 10 >= (y1 - y0 + 1) * 9;
    };

    QVector<QRect> rects;
    std::vector<int> stack;
    for (int start = 0; start < W * H; ++start) {
        if (busy[start] != 1)
            continue;
        int n = 0, x0 = W, y0 = H, x1 = -1, y1 = -1;
        stack.assign(1, start);
        busy[start] = 2;
        while (!stack.empty()) {
            const int c = stack.back();
            stack.pop_back();
            const int cx = c % W, cy = c / W;
            ++n;
            x0 = std::min(x0, cx); x1 = std::max(x1, cx);
            y0 = std::min(y0, cy); y1 = std::max(y1, cy);
            // Two blocks apart still joins: a still's flat patches (a dark backdrop, a shirt) are
            // not busy, and must not cut it in two.
            for (int dy = -2; dy <= 2; ++dy)
                for (int dx = -2; dx <= 2; ++dx) {
                    const int nx = cx + dx, ny = cy + dy;
                    if (nx >= 0 && ny >= 0 && nx < W && ny < H && busy[size_t(ny) * W + nx] == 1) {
                        busy[size_t(ny) * W + nx] = 2;
                        stack.push_back(ny * W + nx);
                    }
                }
        }
        const int bw = x1 - x0 + 1, bh = y1 - y0 + 1;
        if (n < 16 || bw < 4 || bh < 4 || n * 10 < bw * bh * 3)
            continue;   // a stray busy block or a scatter: not a picture
        QRect r = QRect(x0 * B, y0 * B, bw * B, bh * B).adjusted(-B, -B, B, B) & img.rect();
        const QRect page = img.rect();
        for (int k = 0; k < 4 * B && r.top() > page.top() && !rowIs(paper, r.top(), r.left(), r.right()); ++k)
            r.setTop(r.top() - 1);
        for (int k = 0; k < 4 * B && r.bottom() < page.bottom() && !rowIs(paper, r.bottom(), r.left(), r.right()); ++k)
            r.setBottom(r.bottom() + 1);
        for (int k = 0; k < 4 * B && r.left() > page.left() && !colIs(paper, r.left(), r.top(), r.bottom()); ++k)
            r.setLeft(r.left() - 1);
        for (int k = 0; k < 4 * B && r.right() < page.right() && !colIs(paper, r.right(), r.top(), r.bottom()); ++k)
            r.setRight(r.right() + 1);
        for (int k = 0; k < 2 * B && r.height() > 2 * B && rowIs(paper, r.top(), r.left(), r.right()); ++k)
            r.setTop(r.top() + 1);
        for (int k = 0; k < 2 * B && r.height() > 2 * B && rowIs(paper, r.bottom(), r.left(), r.right()); ++k)
            r.setBottom(r.bottom() - 1);
        for (int k = 0; k < 2 * B && r.width() > 2 * B && colIs(paper, r.left(), r.top(), r.bottom()); ++k)
            r.setLeft(r.left() + 1);
        for (int k = 0; k < 2 * B && r.width() > 2 * B && colIs(paper, r.right(), r.top(), r.bottom()); ++k)
            r.setRight(r.right() - 1);
        rects.append(r);
    }
    // Boxes that touch become one: tiles never overlap.
    for (bool merged = true; merged;) {
        merged = false;
        for (int i = 0; i < rects.size() && !merged; ++i)
            for (int j = i + 1; j < rects.size() && !merged; ++j)
                if (rects[i].intersects(rects[j])) {
                    rects[i] = rects[i] | rects[j];
                    rects.removeAt(j);
                    merged = true;
                }
    }
    return rects;
}

// The page as an indexed (palette) image, photographs aside. Inside each photo box, inset by
// kSeamRim, the palette page holds one flat colour (the photograph's mean) — the JPEG tile
// covers it — and the rim keeps the photograph's own pixels, quantised, so a viewer that blends
// a tile's edge with what lies under it blends like with like and draws no seam.
struct Indexed {
    PdfImage image;
    int colours = 0;
    bool exact = false;
    int bits = 8;
    int maxError = 0;
    double badShare = 0;
};

Indexed indexedPage(const QImage &img, const QVector<QRect> &photos)
{
    const int w = img.width(), h = img.height();
    // Per row: 0 = page, 1 = photo rim (mapped to its nearest entry), 2 + k = photo k's inside.
    std::vector<int> cls(w);
    auto classify = [&](int y) -> bool {
        bool any = false;
        std::fill(cls.begin(), cls.end(), 0);
        for (int k = 0; k < photos.size(); ++k) {
            const QRect &r = photos[k];
            if (y < r.top() || y > r.bottom())
                continue;
            any = true;
            const QRect in = r.adjusted(kSeamRim, kSeamRim, -kSeamRim, -kSeamRim);
            const bool inRow = in.isValid() && y >= in.top() && y <= in.bottom();
            for (int x = r.left(); x <= r.right(); ++x)
                cls[x] = (inRow && x >= in.left() && x <= in.right()) ? 2 + k : 1;
        }
        return any;
    };

    // The histogram of the page outside the photographs, counted in runs (most of a page is
    // long runs of paper).
    ColourTable table;
    for (int y = 0; y < h; ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        const bool mixed = !photos.isEmpty() && classify(y);
        QRgb run = 0;
        quint32 len = 0;
        for (int x = 0; x < w; ++x) {
            if (mixed && cls[x])
                continue;
            if (len && line[x] == run) {
                ++len;
                continue;
            }
            if (len)
                table.add(run, len);
            run = line[x];
            len = 1;
        }
        if (len)
            table.add(run, len);
    }

    // Each photograph's flat stand-in.
    QVector<QRgb> flats;
    for (const QRect &r : photos) {
        const QRect in = r.adjusted(kSeamRim, kSeamRim, -kSeamRim, -kSeamRim);
        double s[3] = {0, 0, 0};
        qint64 n = 0;
        if (in.isValid()) {
            for (int y = in.top(); y <= in.bottom(); y += 4) {
                const auto *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
                for (int x = in.left(); x <= in.right(); x += 4, ++n) {
                    s[0] += qRed(line[x]); s[1] += qGreen(line[x]); s[2] += qBlue(line[x]);
                }
            }
        }
        flats.append(n ? qRgb(int(s[0] / n + 0.5), int(s[1] / n + 0.5), int(s[2] / n + 0.5)) : qRgb(255, 255, 255));
    }

    struct Entry { int r, g, b; quint32 n; ColourTable::Slot *slot; };
    std::vector<Entry> entries;
    entries.reserve(table.size());
    for (ColourTable::Slot &s : table.all())
        if (s.key)
            entries.push_back({qRed(s.key), qGreen(s.key), qBlue(s.key), s.count, &s});

    QVector<QRgb> palette;
    std::vector<int> pr, pg, pb;
    auto nearest = [&](int r, int g, int b) {
        int best = 0, bestD = INT_MAX;
        for (size_t i = 0; i < pr.size(); ++i) {
            const int dr = r - pr[i], dg = g - pg[i], db = b - pb[i];
            const int d = dr * dr + dg * dg + db * db;
            if (d < bestD) {
                bestD = d;
                best = int(i);
            }
        }
        return best;
    };
    auto setPalette = [&]() {
        pr.resize(palette.size()); pg.resize(palette.size()); pb.resize(palette.size());
        for (int i = 0; i < palette.size(); ++i) {
            pr[i] = qRed(palette[i]); pg[i] = qGreen(palette[i]); pb[i] = qBlue(palette[i]);
        }
    };

    Indexed out;
    QVector<QRgb> extraFlats;   // flats that are not already a page colour
    for (QRgb f : flats)
        if (!table.find(f) && !extraFlats.contains(f))
            extraFlats.append(f);

    if (entries.size() + size_t(extraFlats.size()) <= 256) {
        // Few enough colours to keep every one: the page is stored exactly.
        for (Entry &e : entries) {
            e.slot->index = palette.size();
            palette.append(qRgb(e.r, e.g, e.b));
        }
        palette += extraFlats;
        setPalette();
        out.exact = true;
    } else {
        // Quantise. The colours a page is printed in — paper, ink, rules, the accent, dot fills —
        // cover thousands of pixels each and are kept exactly; the rest (the anti-aliased edges
        // between them) are split by weighted median cut into what is left of 256, refined by a
        // few k-means passes. No dithering: it would speckle flat colour and fur the text.
        std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.n > b.n; });
        int fixed = 0;
        palette += extraFlats;
        for (Entry &e : entries) {
            if (e.n < quint32(kSolidPixels) || palette.size() >= kMaxSolid)
                break;
            palette.append(qRgb(e.r, e.g, e.b));
            ++fixed;
        }
        const int forced = palette.size();

        struct Box { size_t lo, hi; double n, sse, mean[3]; int axis; };
        auto stats = [&](Box &b) {
            double n = 0, s[3] = {0, 0, 0}, s2[3] = {0, 0, 0};
            for (size_t i = b.lo; i < b.hi; ++i) {
                const double wgt = entries[i].n;
                const int v[3] = {entries[i].r, entries[i].g, entries[i].b};
                n += wgt;
                for (int c = 0; c < 3; ++c) {
                    s[c] += wgt * v[c];
                    s2[c] += wgt * v[c] * v[c];
                }
            }
            b.n = n;
            b.sse = 0;
            b.axis = 0;
            double best = -1;
            for (int c = 0; c < 3; ++c) {
                b.mean[c] = n > 0 ? s[c] / n : 0;
                const double var = n > 0 ? s2[c] - s[c] * s[c] / n : 0;
                b.sse += var;
                if (var > best) {
                    best = var;
                    b.axis = c;
                }
            }
        };
        std::vector<Box> boxes;
        Box all{size_t(fixed), entries.size(), 0, 0, {0, 0, 0}, 0};
        stats(all);
        boxes.push_back(all);
        while (int(boxes.size()) < 256 - forced) {
            int pick = -1;
            for (int i = 0; i < int(boxes.size()); ++i)
                if (boxes[i].hi - boxes[i].lo >= 2 && boxes[i].sse > 0
                    && (pick < 0 || boxes[i].sse > boxes[pick].sse))
                    pick = i;
            if (pick < 0)
                break;
            Box b = boxes[pick];
            const int axis = b.axis;
            std::sort(entries.begin() + b.lo, entries.begin() + b.hi, [axis](const Entry &p, const Entry &q) {
                return (axis == 0 ? p.r : axis == 1 ? p.g : p.b) < (axis == 0 ? q.r : axis == 1 ? q.g : q.b);
            });
            double cum = 0;
            size_t mid = b.lo + 1;
            for (size_t i = b.lo; i < b.hi; ++i) {
                cum += entries[i].n;
                if (cum >= b.n / 2) {
                    mid = i + 1;
                    break;
                }
            }
            mid = std::clamp(mid, b.lo + 1, b.hi - 1);
            Box lo{b.lo, mid, 0, 0, {0, 0, 0}, 0}, hi{mid, b.hi, 0, 0, {0, 0, 0}, 0};
            stats(lo);
            stats(hi);
            boxes[pick] = lo;
            boxes.push_back(hi);
        }
        for (const Box &b : boxes)
            palette.append(qRgb(int(b.mean[0] + 0.5), int(b.mean[1] + 0.5), int(b.mean[2] + 0.5)));
        setPalette();

        const int passes = entries.size() <= 60000 ? 3 : 1;
        for (int pass = 0; pass < passes; ++pass) {
            std::vector<double> acc(size_t(palette.size()) * 4, 0.0);
            for (const Entry &e : entries) {
                const int k = nearest(e.r, e.g, e.b);
                double *a = &acc[size_t(k) * 4];
                a[0] += double(e.n) * e.r; a[1] += double(e.n) * e.g; a[2] += double(e.n) * e.b; a[3] += e.n;
            }
            for (int k = forced; k < palette.size(); ++k) {
                const double *a = &acc[size_t(k) * 4];
                if (a[3] > 0)
                    palette[k] = qRgb(int(a[0] / a[3] + 0.5), int(a[1] / a[3] + 0.5), int(a[2] / a[3] + 0.5));
            }
            setPalette();
        }
        for (Entry &e : entries)
            e.slot->index = nearest(e.r, e.g, e.b);
    }

    // How far the palette moved the page.
    double total = 0, bad = 0;
    for (const Entry &e : entries) {
        const QRgb p = palette[e.slot->index];
        const int err = std::max({std::abs(e.r - qRed(p)), std::abs(e.g - qGreen(p)), std::abs(e.b - qBlue(p))});
        out.maxError = std::max(out.maxError, err);
        total += e.n;
        if (err > kBadError)
            bad += e.n;
    }
    out.badShare = total > 0 ? bad / total : 0;

    QVector<int> flatIndex;
    for (QRgb f : flats) {
        const ColourTable::Slot *s = table.find(f);
        flatIndex.append(s ? s->index : int(palette.indexOf(f)));
    }

    const int n = palette.size();
    out.bits = n <= 2 ? 1 : n <= 4 ? 2 : n <= 16 ? 4 : 8;
    out.colours = n;
    const int bits = out.bits;
    const qsizetype rowBytes = (qsizetype(w) * bits + 7) / 8;
    QByteArray raw(rowBytes * h, '\0');
    for (int y = 0; y < h; ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        auto *row = reinterpret_cast<uchar *>(raw.data()) + rowBytes * y;
        const bool mixed = !photos.isEmpty() && classify(y);
        QRgb prev = ~line[0];
        int prevIndex = 0;
        for (int x = 0; x < w; ++x) {
            int idx;
            const int c = mixed ? cls[x] : 0;
            if (c >= 2) {
                idx = flatIndex[c - 2];
            } else if (c == 1) {
                idx = nearest(qRed(line[x]), qGreen(line[x]), qBlue(line[x]));
            } else if (line[x] == prev) {
                idx = prevIndex;
            } else {
                const ColourTable::Slot *s = table.find(line[x]);
                idx = s ? s->index : 0;
                prev = line[x];
                prevIndex = idx;
            }
            if (bits == 8)
                row[x] = uchar(idx);
            else
                row[(x * bits) >> 3] |= uchar(idx << (8 - bits - ((x * bits) & 7)));
        }
    }

    QByteArray lookup;
    lookup.reserve(n * 3);
    for (QRgb c : palette) {
        lookup.append(char(qRed(c)));
        lookup.append(char(qGreen(c)));
        lookup.append(char(qBlue(c)));
    }
    out.image.px = img.rect();
    out.image.dict = "/Width " + QByteArray::number(w) + " /Height " + QByteArray::number(h)
                     + " /ColorSpace [/Indexed /DeviceRGB " + QByteArray::number(n - 1) + " <"
                     + lookup.toHex().toUpper() + ">] /BitsPerComponent " + QByteArray::number(bits)
                     + " /Filter /FlateDecode";
    out.image.data = flate(raw);
    return out;
}

// A page with no palette that will do (a gradient nobody flagged): stored as Flate RGB, as
// "lossless" would have.
PdfImage rgbPage(const QImage &img)
{
    PdfImage im;
    im.px = img.rect();
    QByteArray rgb;
    rgb.reserve(qsizetype(img.width()) * img.height() * 3);
    for (int y = 0; y < img.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            rgb.append(char(qRed(line[x])));
            rgb.append(char(qGreen(line[x])));
            rgb.append(char(qBlue(line[x])));
        }
    }
    im.dict = "/Width " + QByteArray::number(img.width()) + " /Height " + QByteArray::number(img.height())
              + " /BitsPerComponent 8 /ColorSpace /DeviceRGB /Filter /FlateDecode";
    im.data = flate(rgb);
    return im;
}

// The "auto" choice for one page (an opaque RGB32 image).
EncodedPage encodePage(const QImage &img, bool photo)
{
    EncodedPage page;
    const double area = double(img.width()) * img.height();
    auto photoArea = [](const QVector<QRect> &rs) {
        double a = 0;
        for (const QRect &r : rs)
            a += double(r.width()) * r.height();
        return a;
    };
    QVector<QRect> photos = photo ? findPhotos(img) : QVector<QRect>();
    QString kind;
    if (photo && photoArea(photos) > 0.6 * area) {
        // The photograph is the page: all of it as JPEG. (A flagged page whose photograph is
        // not found is a palette page like any other; the check below catches one the palette
        // cannot hold.)
        page.images.append(photoImage(img, img.rect()));
        kind = QStringLiteral("jpeg");
        page.info.insert(QStringLiteral("photos"), 1);
    } else {
        Indexed ix = indexedPage(img, photos);
        if (photos.isEmpty() && ix.badShare > kBadShare) {
            // Not flagged, but no palette holds it: look for a photograph anyway, and failing
            // that keep every colour.
            const QVector<QRect> found = findPhotos(img);
            if (!found.isEmpty() && photoArea(found) <= 0.6 * area) {
                photos = found;
                ix = indexedPage(img, photos);
            }
        }
        if (ix.badShare > kBadShare) {
            page.images.append(rgbPage(img));
            kind = QStringLiteral("rgb");
            photos.clear();
        } else {
            page.images.append(ix.image);
            for (const QRect &r : photos)
                page.images.append(photoImage(img, r));
            kind = photos.isEmpty() ? QStringLiteral("palette") : QStringLiteral("palette+jpeg");
            page.info.insert(QStringLiteral("colours"), ix.colours);
            page.info.insert(QStringLiteral("exact"), ix.exact);
            page.info.insert(QStringLiteral("bitsPerPixel"), ix.bits);
            page.info.insert(QStringLiteral("maxError"), ix.maxError);
        }
        page.info.insert(QStringLiteral("photos"), int(photos.size()));
    }
    qint64 bytes = 0;
    for (const PdfImage &im : page.images)
        bytes += im.data.size();
    page.info.insert(QStringLiteral("encoding"), kind);
    page.info.insert(QStringLiteral("bytes"), bytes);
    return page;
}

// The file. Objects 1–6 are fixed (Info, XMP, ICC profile, output intent, catalog, page tree);
// each page adds its images, its content stream and itself; finish() writes the page tree, the
// xref and the trailer. Everything is streamed to the device as it is made.
class PdfAFile
{
public:
    struct Meta { QString title, author, subject, creator; };

    PdfAFile(QIODevice *out, const QSizeF &pagePt) : m_out(out), m_pagePt(pagePt)
    {
        m_offsets.fill(-1, 7);
    }

    bool start(const Meta &meta)
    {
        put(QByteArray("%PDF-1.4\n%\xE2\xE3\xCF\xD3\n"));
        const QDateTime now = QDateTime::currentDateTime();
        const int off = now.offsetFromUtc() / 60;
        const QString sign = off < 0 ? QStringLiteral("-") : QStringLiteral("+");
        const QString hh = QStringLiteral("%1").arg(std::abs(off) / 60, 2, 10, QLatin1Char('0'));
        const QString mm = QStringLiteral("%1").arg(std::abs(off) % 60, 2, 10, QLatin1Char('0'));
        const QByteArray pdfDate = ("D:" + now.toString(QStringLiteral("yyyyMMddHHmmss")) + sign + hh + '\'' + mm + '\'').toLatin1();
        const QString xmpDate = now.toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss")) + sign + hh + ':' + mm;
        const QString producer = QStringLiteral("PinPoint Studio PDF writer (Qt %1)").arg(QString::fromLatin1(qVersion()));
        const QUuid id = QUuid::createUuid();
        m_id = id.toRfc4122().toHex().toUpper();

        // 1: Info — and every entry in it is said again, equivalently, in the XMP (PDF/A-1 6.7.3).
        QByteArray info = "<< /Title " + textString(meta.title) + " /Creator " + textString(meta.creator)
                          + " /Producer " + textString(producer) + " /CreationDate (" + pdfDate
                          + ") /ModDate (" + pdfDate + ")";
        if (!meta.author.isEmpty())
            info += " /Author " + textString(meta.author);
        if (!meta.subject.isEmpty())
            info += " /Subject " + textString(meta.subject);
        object(1, info + " >>");

        // 2: XMP, unfiltered (6.7.2), predefined schemas only.
        auto esc = [](const QString &s) { return s.toHtmlEscaped(); };
        // The packet header's begin="" holds U+FEFF, which the UTF-8 encoding below makes EF BB BF.
        QString xmp = QStringLiteral("<?xpacket begin=\"") + QChar(0xFEFF) + QStringLiteral(
            "\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
            "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n"
            " <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
            "  <rdf:Description rdf:about=\"\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n"
            "   <dc:format>application/pdf</dc:format>\n"
            "   <dc:title><rdf:Alt><rdf:li xml:lang=\"x-default\">%1</rdf:li></rdf:Alt></dc:title>\n").arg(esc(meta.title));
        if (!meta.author.isEmpty())
            xmp += QStringLiteral("   <dc:creator><rdf:Seq><rdf:li>%1</rdf:li></rdf:Seq></dc:creator>\n").arg(esc(meta.author));
        if (!meta.subject.isEmpty())
            xmp += QStringLiteral("   <dc:description><rdf:Alt><rdf:li xml:lang=\"x-default\">%1</rdf:li></rdf:Alt></dc:description>\n").arg(esc(meta.subject));
        xmp += QStringLiteral(
            "  </rdf:Description>\n"
            "  <rdf:Description rdf:about=\"\" xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\">\n"
            "   <xmp:CreatorTool>%1</xmp:CreatorTool>\n"
            "   <xmp:CreateDate>%2</xmp:CreateDate>\n"
            "   <xmp:ModifyDate>%2</xmp:ModifyDate>\n"
            "   <xmp:MetadataDate>%2</xmp:MetadataDate>\n"
            "  </rdf:Description>\n"
            "  <rdf:Description rdf:about=\"\" xmlns:pdf=\"http://ns.adobe.com/pdf/1.3/\">\n"
            "   <pdf:Producer>%3</pdf:Producer>\n"
            "  </rdf:Description>\n"
            "  <rdf:Description rdf:about=\"\" xmlns:xmpMM=\"http://ns.adobe.com/xap/1.0/mm/\">\n"
            "   <xmpMM:DocumentID>uuid:%4</xmpMM:DocumentID>\n"
            "  </rdf:Description>\n"
            "  <rdf:Description rdf:about=\"\" xmlns:pdfaid=\"http://www.aiim.org/pdfa/ns/id/\">\n"
            "   <pdfaid:part>1</pdfaid:part>\n"
            "   <pdfaid:conformance>B</pdfaid:conformance>\n"
            "  </rdf:Description>\n"
            " </rdf:RDF>\n"
            "</x:xmpmeta>\n"
            "<?xpacket end=\"w\"?>")
                   .arg(esc(meta.creator), xmpDate, esc(producer), id.toString(QUuid::WithoutBraces));
        stream(2, " /Type /Metadata /Subtype /XML", xmp.toUtf8());

        // 3–4: the output intent, with the sRGB profile QPdfWriter itself embeds.
        QByteArray icc;
        QFile qtIcc(QStringLiteral(":/qpdf/sRGB2014.icc"));
        if (qtIcc.open(QIODevice::ReadOnly))
            icc = qtIcc.readAll();
        if (icc.isEmpty())
            icc = QColorSpace(QColorSpace::SRgb).iccProfile();
        if (icc.isEmpty())
            m_ok = false;
        stream(3, " /N 3 /Alternate /DeviceRGB /Filter /FlateDecode", flate(icc));
        object(4, "<< /Type /OutputIntent /S /GTS_PDFA1 /DestOutputProfile 3 0 R"
                  " /OutputConditionIdentifier (sRGB IEC61966-2.1) /Info (sRGB IEC61966-2.1)"
                  " /RegistryName (http://www.color.org) >>");
        object(5, "<< /Type /Catalog /Pages 6 0 R /Metadata 2 0 R /OutputIntents [4 0 R] >>");
        return m_ok;
    }

    bool addPage(const QVector<PdfImage> &images, const QSize &pagePx)
    {
        QSizeF fitted = QSizeF(pagePx);
        fitted.scale(m_pagePt, Qt::KeepAspectRatio);
        const double x0 = (m_pagePt.width() - fitted.width()) / 2.0;
        const double top0 = (m_pagePt.height() - fitted.height()) / 2.0;
        const double s = fitted.width() / pagePx.width();

        QByteArray xobjects, content;
        for (int i = 0; i < images.size(); ++i) {
            const PdfImage &im = images[i];
            const int n = allocate();
            stream(n, " /Type /XObject /Subtype /Image " + im.dict, im.data);
            xobjects += " /Im" + QByteArray::number(i) + ' ' + QByteArray::number(n) + " 0 R";
            const double w = im.px.width() * s, h = im.px.height() * s;
            const double x = x0 + im.px.x() * s;
            const double y = m_pagePt.height() - (top0 + im.px.y() * s) - h;
            content += "q " + num(w) + " 0 0 " + num(h) + ' ' + num(x) + ' ' + num(y) + " cm /Im"
                       + QByteArray::number(i) + " Do Q\n";
        }
        const int contents = allocate();
        stream(contents, QByteArray(), content);
        const int page = allocate();
        object(page, "<< /Type /Page /Parent 6 0 R /MediaBox [0 0 " + num(m_pagePt.width()) + ' '
                         + num(m_pagePt.height()) + "] /Resources << /XObject <<" + xobjects
                         + " >> >> /Contents " + QByteArray::number(contents) + " 0 R >>");
        m_pages.append(page);
        return m_ok;
    }

    bool finish()
    {
        QByteArray kids;
        for (int p : m_pages)
            kids += QByteArray::number(p) + " 0 R ";
        object(6, "<< /Type /Pages /Kids [" + kids.trimmed() + "] /Count " + QByteArray::number(m_pages.size()) + " >>");
        const qint64 xref = m_pos;
        QByteArray table = "xref\n0 " + QByteArray::number(m_offsets.size()) + "\n0000000000 65535 f \n";
        for (int i = 1; i < m_offsets.size(); ++i) {
            if (m_offsets[i] < 0)
                m_ok = false;   // an object number was given out and never written
            table += QByteArray::number(m_offsets[i]).rightJustified(10, '0') + " 00000 n \n";
        }
        put(table);
        put("trailer\n<< /Size " + QByteArray::number(m_offsets.size()) + " /Root 5 0 R /Info 1 0 R /ID [<"
            + m_id + "> <" + m_id + ">] >>\nstartxref\n" + QByteArray::number(xref) + "\n%%EOF\n");
        return m_ok;
    }

private:
    int allocate()
    {
        m_offsets.append(-1);
        return int(m_offsets.size()) - 1;
    }
    void put(const QByteArray &b)
    {
        if (!m_ok)
            return;
        if (m_out->write(b) != b.size())
            m_ok = false;
        m_pos += b.size();
    }
    void open(int n)
    {
        m_offsets[n] = m_pos;
        put(QByteArray::number(n) + " 0 obj\n");
    }
    void object(int n, const QByteArray &body)
    {
        open(n);
        put(body);
        put("\nendobj\n");
    }
    void stream(int n, const QByteArray &dict, const QByteArray &data)
    {
        open(n);
        put("<<" + dict + " /Length " + QByteArray::number(data.size()) + " >>\nstream\n");
        put(data);
        put("\nendstream\nendobj\n");
    }

    QIODevice *m_out;
    QSizeF m_pagePt;
    QVector<qint64> m_offsets;   // by object number; [0] is the free head
    QVector<int> m_pages;
    QByteArray m_id;
    qint64 m_pos = 0;
    bool m_ok = true;
};

} // namespace pdf_detail

PdfDocumentWriter::PdfDocumentWriter(QObject *parent)
    : QObject(parent)
{
}

PdfDocumentWriter::~PdfDocumentWriter()
{
    // A document still open here was never finished: discard it rather than leave half a PDF.
    if (m_file)
        release(false);
}

void PdfDocumentWriter::setEncoding(const QString &encoding)
{
    if (m_encoding == encoding)
        return;
    m_encoding = encoding;
    emit encodingChanged();
}

void PdfDocumentWriter::setSubject(const QString &subject)
{
    if (m_subject == subject)
        return;
    m_subject = subject;
    emit subjectChanged();
}

void PdfDocumentWriter::setLossless(bool on)
{
    if (m_lossless == on)
        return;
    m_lossless = on;
    emit losslessChanged();
}

bool PdfDocumentWriter::fail(const QString &message)
{
    if (m_error != message) {
        m_error = message;
        emit errorStringChanged();
    }
    return false;
}

void PdfDocumentWriter::clearError()
{
    if (!m_error.isEmpty()) {
        m_error.clear();
        emit errorStringChanged();
    }
}

// Ends the painter (which writes the PDF trailer), drops the writer, then either moves the file
// into place or discards it. Returns through m_error: a failed commit is reported by end().
void PdfDocumentWriter::release(bool commit)
{
    if (m_painter && m_painter->isActive())
        m_painter->end();
    m_painter.reset();
    m_writer.reset();
    bool written = true;
    if (m_raw) {
        if (commit)
            written = m_raw->finish();
        m_raw.reset();
    }
    if (m_file) {
        if (commit && !written) {
            m_file->cancelWriting();
            fail(tr("Could not write %1: %2").arg(m_path, m_file->errorString()));
        } else if (commit) {
            if (!m_file->commit())
                fail(tr("Could not save %1: %2").arg(m_path, m_file->errorString()));
        } else {
            m_file->cancelWriting();
        }
        m_file.reset();
    }
    emit activeChanged();
}

bool PdfDocumentWriter::begin(const QString &path, bool landscape, int dpi,
                              const QString &title, const QString &author)
{
    if (m_file)
        return fail(tr("A PDF is already being written to %1; end or abort it first.").arg(m_path));

    const QString file = localPath(path);
    if (file.isEmpty())
        return fail(tr("No file name was given for the PDF."));
    if (dpi < kMinDpi || dpi > kMaxDpi)
        return fail(tr("%1 dpi is outside %2–%3.").arg(dpi).arg(kMinDpi).arg(kMaxDpi));
    const bool own = m_encoding == QLatin1String("auto");
    if (!own && m_encoding != QLatin1String("lossless"))
        return fail(tr("\"%1\" is not a PDF encoding: use \"lossless\" or \"auto\".").arg(m_encoding));

    auto out = std::make_unique<QSaveFile>(file);
    if (!out->open(QIODevice::WriteOnly))
        return fail(tr("Could not create %1: %2").arg(file, out->errorString()));

    if (own) {
        // A4 is 210 × 297 mm.
        const QSizeF a4(210.0 / 25.4 * 72.0, 297.0 / 25.4 * 72.0);
        auto raw = std::make_unique<pdf_detail::PdfAFile>(out.get(), landscape ? a4.transposed() : a4);
        pdf_detail::PdfAFile::Meta meta;
        meta.title = title.isEmpty() ? QFileInfo(file).completeBaseName() : title;
        meta.author = author;
        meta.subject = m_subject;
        meta.creator = QStringLiteral("PinPoint Studio");
        if (!raw->start(meta)) {
            out->cancelWriting();
            return fail(tr("Could not start writing %1.").arg(file));
        }
        m_raw = std::move(raw);
        m_file = std::move(out);
        m_path = file;
        clearError();
        if (m_pageCount != 0) {
            m_pageCount = 0;
            emit pageCountChanged();
        }
        emit activeChanged();
        return true;
    }

    auto writer = std::make_unique<QPdfWriter>(out.get());
    writer->setPdfVersion(QPagedPaintDevice::PdfVersion_A1b);
    writer->setResolution(dpi);
    // The page images carry their own margins, so the sheet has none.
    const QPageLayout layout(QPageSize(QPageSize::A4),
                             landscape ? QPageLayout::Landscape : QPageLayout::Portrait,
                             QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter, QMarginsF(0, 0, 0, 0));
    if (!writer->setPageLayout(layout)) {
        out->cancelWriting();
        return fail(tr("Could not set an A4 page layout."));
    }
    writer->setTitle(title.isEmpty() ? QFileInfo(file).completeBaseName() : title);
    writer->setCreator(QStringLiteral("PinPoint Studio"));
    if (!author.isEmpty())
        writer->setAuthor(author);

    m_file = std::move(out);
    m_writer = std::move(writer);
    m_path = file;
    clearError();
    if (m_pageCount != 0) {
        m_pageCount = 0;
        emit pageCountChanged();
    }
    emit activeChanged();
    return true;
}

QImage PdfDocumentWriter::imageFrom(const QVariant &v, QString *why, int depth)
{
    const QMetaType type = v.metaType();
    switch (type.id()) {
    case QMetaType::QImage:
        return v.value<QImage>();
    case QMetaType::QPixmap:
        return v.value<QPixmap>().toImage();
    case QMetaType::QUrl:
    case QMetaType::QString: {
        const QString file = type.id() == QMetaType::QUrl ? v.toUrl().toLocalFile()
                                                         : localPath(v.toString());
        QImage img(file);
        if (img.isNull())
            *why = tr("Could not read an image from %1.").arg(file);
        return img;
    }
    default:
        break;
    }
    if (type == QMetaType::fromType<QJSValue>() && depth == 0)
        return imageFrom(v.value<QJSValue>().toVariant(), why, depth + 1);
    // An ItemGrabResult, or anything else that carries its picture as `image`.
    if ((type.flags() & QMetaType::PointerToQObject) && depth == 0) {
        const QObject *obj = *static_cast<QObject *const *>(v.constData());
        if (obj) {
            const QVariant image = obj->property("image");
            if (image.isValid())
                return imageFrom(image, why, depth + 1);
        }
    }
    *why = tr("addPage() needs an image; it was given %1.")
               .arg(type.isValid() ? QString::fromLatin1(type.name()) : QStringLiteral("nothing"));
    return {};
}

bool PdfDocumentWriter::addPage(const QVariant &image, bool photo, const QSize &requested)
{
    if (!m_file)
        return fail(tr("addPage() was called before begin()."));
    QString why;
    QImage img = imageFrom(image, &why);
    if (img.isNull())
        return fail(why.isEmpty() ? tr("addPage() was given an empty image.") : why);
    // A HiDPI grab (QQuickItem::grabToImage on a window with devicePixelRatio > 1) comes back a whole
    // multiple of the size asked for, and Qt 6.11 does it two ways, seen on one Mac minutes apart: the page
    // drawn at the requested size in the top-left corner with blank paper around it, or the page scaled up
    // to fill the bigger image. Blank paper outside the corner means the first (keep the corner); anything
    // drawn there means the second (scale the whole image back down). Either way the page comes out at the
    // size the caller asked for, which is what fixes its resolution on the sheet.
    if (requested.isValid() && img.width() >= requested.width() * 3 / 2 && img.height() >= requested.height() * 3 / 2) {
        const QImage rgb = img.convertToFormat(QImage::Format_RGB32);
        bool blankOutside = true;
        for (int y = 0; y < rgb.height() && blankOutside; y += 3) {
            const QRgb *row = reinterpret_cast<const QRgb *>(rgb.constScanLine(y));
            for (int x = (y < requested.height() ? requested.width() : 0); x < rgb.width(); x += 3)
                if (qRed(row[x]) < 245 || qGreen(row[x]) < 245 || qBlue(row[x]) < 245) { blankOutside = false; break; }
        }
        img = blankOutside ? img.copy(QRect(QPoint(0, 0), requested))
                           : img.scaled(requested, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        img.setDevicePixelRatio(1.0);
    }
    return addImage(img, photo);
}

bool PdfDocumentWriter::addImage(const QImage &image, bool photo)
{
    if (!m_file)
        return fail(tr("addPage() was called before begin()."));
    if (image.isNull() || image.width() <= 0 || image.height() <= 0)
        return fail(tr("addPage() was given an empty image."));

    // PDF/A has no transparency: lay the page on white paper.
    QImage page = image;
    if (page.hasAlphaChannel()) {
        QImage flat(page.size(), QImage::Format_RGB32);
        flat.fill(Qt::white);
        QPainter p(&flat);
        p.drawImage(0, 0, page);
        p.end();
        page = flat;
    }

    if (m_raw) {
        const pdf_detail::EncodedPage encoded =
            pdf_detail::encodePage(page.convertToFormat(QImage::Format_RGB32), photo);
        if (!m_raw->addPage(encoded.images, page.size()))
            return fail(tr("Could not add page %1 to %2.").arg(m_pageCount + 1).arg(m_path));
        m_lastPage = encoded.info;
        ++m_pageCount;
        emit pageCountChanged();
        return true;
    }

    if (!m_painter) {
        // The painter opens the first page; it starts on the first image, so a document with no
        // pages is never begun and end() can refuse it without leaving a blank sheet behind.
        m_painter = std::make_unique<QPainter>();
        if (!m_painter->begin(m_writer.get())) {
            m_painter.reset();
            return fail(tr("Could not start writing %1.").arg(m_path));
        }
    } else if (!m_writer->newPage()) {
        return fail(tr("Could not add page %1 to %2.").arg(m_pageCount + 1).arg(m_path));
    }

    m_painter->setRenderHint(QPainter::LosslessImageRendering, m_lossless);
    m_painter->setRenderHint(QPainter::SmoothPixmapTransform, true);

    // The painter's coordinates are the sheet in device pixels at the writer's resolution.
    const QSizeF sheet(m_writer->width(), m_writer->height());
    QSizeF fitted = QSizeF(page.size());
    fitted.scale(sheet, Qt::KeepAspectRatio);
    const QRectF target(QPointF((sheet.width() - fitted.width()) / 2.0,
                                (sheet.height() - fitted.height()) / 2.0),
                        fitted);
    m_painter->drawImage(target, page);
    m_lastPage = QVariantMap{{QStringLiteral("encoding"), m_lossless ? QStringLiteral("rgb") : QStringLiteral("qt-jpeg")}};

    ++m_pageCount;
    emit pageCountChanged();
    return true;
}

bool PdfDocumentWriter::end()
{
    if (!m_file)
        return fail(tr("end() was called before begin()."));
    if (m_pageCount == 0) {
        const QString path = m_path;
        release(false);
        return fail(tr("No pages were added, so %1 was not written.").arg(path));
    }
    clearError();
    release(true);
    return m_error.isEmpty();
}

void PdfDocumentWriter::abort()
{
    if (m_file)
        release(false);
}
