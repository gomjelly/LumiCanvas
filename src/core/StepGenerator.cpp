#include "core/StepGenerator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <random>
#include <vector>

namespace lumi {

namespace {

// A projector can't show black, so a region painted in a dark colour would be
// invisible and its step would look empty. Every region is lifted above black.
constexpr float kMinDisplayLevel = 0.22f;

constexpr int kMaxKMeansSamples = 20000;
constexpr int kKMeansIterations = 15;

using Plane = std::vector<float>;
using Vec3 = std::array<float, 3>;

struct LabPlanes {
    Plane l, a, b;
};

// Flattens transparency onto white, as the image would look on paper.
QImage prepareSource(const QImage& source, int maxSide)
{
    QImage image = source;
    if (std::max(image.width(), image.height()) > maxSide)
        image = image.scaled(maxSide, maxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    image = image.convertToFormat(QImage::Format_RGBA8888);
    for (int y = 0; y < image.height(); ++y) {
        uchar* p = image.scanLine(y);
        for (int x = 0; x < image.width(); ++x, p += 4) {
            const int alpha = p[3];
            for (int c = 0; c < 3; ++c)
                p[c] = uchar((p[c] * alpha + 255 * (255 - alpha)) / 255);
            p[3] = 255;
        }
    }
    return image;
}

LabPlanes toLab(const QImage& image, bool colorMatters)
{
    std::array<float, 256> linear;
    for (int i = 0; i < 256; ++i) {
        const float s = i / 255.0f;
        linear[i] = s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
    }
    const auto f = [](float t) { return t > 0.008856f ? std::cbrt(t) : 7.787f * t + 16.0f / 116.0f; };

    const size_t count = size_t(image.width()) * image.height();
    LabPlanes lab{Plane(count), Plane(count), Plane(count)};
    size_t i = 0;
    for (int y = 0; y < image.height(); ++y) {
        const uchar* p = image.constScanLine(y);
        for (int x = 0; x < image.width(); ++x, p += 4, ++i) {
            const float r = linear[p[0]], g = linear[p[1]], b = linear[p[2]];
            const float fx = f((0.4124f * r + 0.3576f * g + 0.1805f * b) / 0.95047f);
            const float fy = f(0.2126f * r + 0.7152f * g + 0.0722f * b);
            const float fz = f((0.0193f * r + 0.1192f * g + 0.9505f * b) / 1.08883f);
            lab.l[i] = 116.0f * fy - 16.0f;
            if (colorMatters) { // left at zero, clustering groups by brightness alone
                lab.a[i] = 500.0f * (fx - fy);
                lab.b[i] = 200.0f * (fy - fz);
            }
        }
    }
    return lab;
}

// One running-sum box blur along each row of src, written transposed so that two
// calls cover both axes without a second code path.
void boxBlurTranspose(const Plane& src, Plane& dst, int w, int h, int radius)
{
    const float norm = 1.0f / (2 * radius + 1);
    for (int y = 0; y < h; ++y) {
        const float* row = src.data() + size_t(y) * w;
        float sum = row[0] * (radius + 1);
        for (int k = 1; k <= radius; ++k)
            sum += row[std::min(k, w - 1)];
        for (int x = 0; x < w; ++x) {
            dst[size_t(x) * h + y] = sum * norm;
            sum += row[std::min(x + radius + 1, w - 1)] - row[std::max(x - radius, 0)];
        }
    }
}

// Box radius whose three passes have the given sigma (variance is r^2 + r).
int radiusForSigma(float sigma)
{
    return int(std::round((std::sqrt(4.0f * sigma * sigma + 1.0f) - 1.0f) / 2.0f));
}

// Three box passes approximate a Gaussian closely and cost the same at any radius.
Plane boxBlur(const Plane& plane, int w, int h, int radius)
{
    Plane result = plane;
    if (radius < 1)
        return result;
    Plane scratch(plane.size());
    for (int pass = 0; pass < 3; ++pass) {
        boxBlurTranspose(result, scratch, w, h, radius);
        boxBlurTranspose(scratch, result, h, w, radius);
    }
    return result;
}

Plane gaussianBlur(const Plane& plane, int w, int h, float sigma)
{
    return boxBlur(plane, w, h, radiusForSigma(sigma));
}

float distanceSquared(const Vec3& a, const Vec3& b)
{
    const float dl = a[0] - b[0], da = a[1] - b[1], db = a[2] - b[2];
    return dl * dl + da * da + db * db;
}

// k-means on an even sample of the image, seeded k-means++ style with a fixed seed.
std::vector<Vec3> clusterColors(const LabPlanes& lab, int k)
{
    const size_t count = lab.l.size();
    const size_t stride = std::max<size_t>(1, count / kMaxKMeansSamples);
    std::vector<Vec3> samples;
    samples.reserve(count / stride + 1);
    for (size_t i = 0; i < count; i += stride)
        samples.push_back({lab.l[i], lab.a[i], lab.b[i]});

    std::mt19937 random(12345);
    std::vector<Vec3> centers{samples[samples.size() / 2]};
    std::vector<float> nearest(samples.size());
    while (int(centers.size()) < k) {
        double total = 0.0;
        for (size_t s = 0; s < samples.size(); ++s) {
            float best = std::numeric_limits<float>::max();
            for (const Vec3& c : centers)
                best = std::min(best, distanceSquared(samples[s], c));
            nearest[s] = best;
            total += best;
        }
        if (total < 1e-3)
            break; // the image has fewer distinct colours than requested
        double pick = std::uniform_real_distribution<double>(0.0, total)(random);
        size_t chosen = samples.size() - 1;
        for (size_t s = 0; s < samples.size(); ++s) {
            pick -= nearest[s];
            if (pick <= 0.0) {
                chosen = s;
                break;
            }
        }
        centers.push_back(samples[chosen]);
    }

    for (int iteration = 0; iteration < kKMeansIterations; ++iteration) {
        std::vector<std::array<double, 3>> sums(centers.size(), {0.0, 0.0, 0.0});
        std::vector<int> counts(centers.size(), 0);
        for (const Vec3& sample : samples) {
            size_t best = 0;
            float bestDistance = std::numeric_limits<float>::max();
            for (size_t c = 0; c < centers.size(); ++c) {
                const float d = distanceSquared(sample, centers[c]);
                if (d < bestDistance) {
                    bestDistance = d;
                    best = c;
                }
            }
            for (int ch = 0; ch < 3; ++ch)
                sums[best][ch] += sample[ch];
            ++counts[best];
        }
        for (size_t c = 0; c < centers.size(); ++c) {
            if (counts[c] > 0) {
                for (int ch = 0; ch < 3; ++ch)
                    centers[c][ch] = float(sums[c][ch] / counts[c]);
            }
        }
    }
    return centers;
}

std::vector<uint8_t> assignLabels(const LabPlanes& lab, const std::vector<Vec3>& centers)
{
    std::vector<uint8_t> labels(lab.l.size());
    for (size_t i = 0; i < labels.size(); ++i) {
        const Vec3 pixel{lab.l[i], lab.a[i], lab.b[i]};
        float bestDistance = std::numeric_limits<float>::max();
        for (size_t c = 0; c < centers.size(); ++c) {
            const float d = distanceSquared(pixel, centers[c]);
            if (d < bestDistance) {
                bestDistance = d;
                labels[i] = uint8_t(c);
            }
        }
    }
    return labels;
}

// Specks too small to paint join whichever region surrounds them most.
void mergeSmallRegions(std::vector<uint8_t>& labels, int w, int h, int minArea)
{
    std::vector<uint8_t> visited(labels.size(), 0);
    std::vector<int> pixels;
    std::array<int, 256> neighbors;
    for (int start = 0; start < int(labels.size()); ++start) {
        if (visited[start])
            continue;
        const uint8_t label = labels[start];
        pixels.assign(1, start);
        visited[start] = 1;
        neighbors.fill(0);
        for (size_t head = 0; head < pixels.size(); ++head) {
            const int p = pixels[head];
            const int x = p % w;
            const auto visit = [&](int q) {
                if (labels[q] != label)
                    ++neighbors[labels[q]];
                else if (!visited[q]) {
                    visited[q] = 1;
                    pixels.push_back(q);
                }
            };
            if (x > 0)
                visit(p - 1);
            if (x < w - 1)
                visit(p + 1);
            if (p >= w)
                visit(p - w);
            if (p < (h - 1) * w)
                visit(p + w);
        }
        if (int(pixels.size()) >= minArea)
            continue;
        const int replacement = int(std::max_element(neighbors.begin(), neighbors.end()) - neighbors.begin());
        if (neighbors[replacement] == 0)
            continue; // the region is the whole image
        for (const int p : pixels)
            labels[p] = uint8_t(replacement);
    }
}

// Mask of pixels on the dark side of an edge, plus colour edges of equal brightness,
// found as differences of Gaussians. Sizes scale with the image, so a preview and
// the full-size result show the same lines.
Plane detectLines(const LabPlanes& lab, int w, int h, float scale, int detail)
{
    const float t = std::clamp(detail, 0, 100) / 100.0f;
    const float sigma = scale * (2.2f - 1.3f * t);
    // In Lab lightness units. A step edge responds with only about 13% of its contrast,
    // so at the middle setting this catches edges of roughly 7 units and up.
    const float threshold = 1.6f - 1.3f * t;

    // Small images need radii that still differ, or the difference is all zero.
    const int fineRadius = std::max(1, radiusForSigma(sigma));
    const int coarseRadius = std::max(fineRadius + 1, radiusForSigma(sigma * 1.6f));
    const auto difference = [&](const Plane& plane) {
        Plane fine = boxBlur(plane, w, h, fineRadius);
        const Plane coarse = boxBlur(plane, w, h, coarseRadius);
        for (size_t i = 0; i < fine.size(); ++i)
            fine[i] -= coarse[i];
        return fine;
    };
    const Plane dl = difference(lab.l);
    const Plane da = difference(lab.a);
    const Plane db = difference(lab.b);

    std::vector<uint8_t> mask(dl.size());
    for (size_t i = 0; i < mask.size(); ++i) {
        // Colour counts only where brightness shows no edge, or one boundary draws two lines.
        const bool colorEdge = std::abs(dl[i]) < threshold
                               && (std::abs(da[i]) > threshold * 1.5f || std::abs(db[i]) > threshold * 1.5f);
        mask[i] = dl[i] < -threshold || colorEdge;
    }

    // Drop isolated flecks: lines are connected, noise is not.
    const int minLength = std::max(4, int(scale * scale * 30.0f));
    std::vector<uint8_t> visited(mask.size(), 0);
    std::vector<int> pixels;
    for (int start = 0; start < int(mask.size()); ++start) {
        if (!mask[start] || visited[start])
            continue;
        pixels.assign(1, start);
        visited[start] = 1;
        for (size_t head = 0; head < pixels.size(); ++head) {
            const int p = pixels[head];
            const int x = p % w;
            const int y = p / w;
            for (int ny = std::max(y - 1, 0); ny <= std::min(y + 1, h - 1); ++ny) {
                for (int nx = std::max(x - 1, 0); nx <= std::min(x + 1, w - 1); ++nx) {
                    const int q = ny * w + nx;
                    if (mask[q] && !visited[q]) {
                        visited[q] = 1;
                        pixels.push_back(q);
                    }
                }
            }
        }
        if (int(pixels.size()) < minLength) {
            for (const int p : pixels)
                mask[p] = 0;
        }
    }

    Plane soft(mask.begin(), mask.end());
    return gaussianBlur(soft, w, h, std::max(0.6f, scale * 0.3f));
}

} // namespace

void generateSteps(const QImage& source, const StepOptions& options, const StepSink& sink)
{
    if (source.isNull() || !sink)
        return;

    const QImage image = prepareSource(source, std::max(16, options.maxSide));
    const int w = image.width();
    const int h = image.height();
    const float scale = std::max(w, h) / 1000.0f;
    const bool useColor = options.method == StepOptions::Method::Colors;

    const LabPlanes lab = toLab(image, true);

    // Cluster a slightly smoothed image so texture doesn't fragment the regions.
    const float smoothing = std::max(0.8f, scale * 1.5f);
    LabPlanes smooth{gaussianBlur(lab.l, w, h, smoothing),
                     useColor ? gaussianBlur(lab.a, w, h, smoothing) : Plane(lab.l.size(), 0.0f),
                     useColor ? gaussianBlur(lab.b, w, h, smoothing) : Plane(lab.l.size(), 0.0f)};
    const std::vector<Vec3> centers = clusterColors(smooth, std::clamp(options.levels, 1, 32));
    std::vector<uint8_t> labels = assignLabels(smooth, centers);
    smooth = LabPlanes{};
    mergeSmallRegions(labels, w, h, std::max(4, w * h / 4000));

    // Each region's real colour, measured from the source.
    std::vector<std::array<double, 3>> sums(centers.size(), {0.0, 0.0, 0.0});
    std::vector<qint64> areas(centers.size(), 0);
    {
        size_t i = 0;
        for (int y = 0; y < h; ++y) {
            const uchar* p = image.constScanLine(y);
            for (int x = 0; x < w; ++x, p += 4, ++i) {
                for (int c = 0; c < 3; ++c)
                    sums[labels[i]][c] += p[c];
                ++areas[labels[i]];
            }
        }
    }

    struct Region {
        int label;
        float luma;
        std::array<uchar, 3> display;
    };
    std::vector<Region> regions;
    for (size_t c = 0; c < centers.size(); ++c) {
        if (areas[c] == 0)
            continue; // emptied by merging
        std::array<float, 3> color;
        for (int ch = 0; ch < 3; ++ch)
            color[ch] = float(sums[c][ch] / areas[c] / 255.0);
        const float luma = 0.299f * color[0] + 0.587f * color[1] + 0.114f * color[2];
        if (!useColor)
            color = {luma, luma, luma};
        Region region{int(c), luma, {}};
        for (int ch = 0; ch < 3; ++ch)
            region.display[ch] = uchar(qRound((kMinDisplayLevel + (1.0f - kMinDisplayLevel) * color[ch]) * 255.0f));
        regions.push_back(region);
    }
    std::sort(regions.begin(), regions.end(), [&](const Region& a, const Region& b) {
        return options.darkFirst ? a.luma < b.luma : a.luma > b.luma;
    });

    Plane lines;
    if (options.outline)
        lines = detectLines(lab, w, h, scale, options.lineDetail);

    const int total = int(regions.size()) + (options.outline ? 1 : 0) + (options.finalOriginal ? 1 : 0);
    int index = 0;

    // The fill accumulates region by region; lines are drawn over a copy for each step.
    QImage fill(w, h, QImage::Format_RGBA8888);
    fill.fill(Qt::black);
    const auto withLines = [&] {
        if (lines.empty())
            return fill;
        QImage result = fill;
        size_t i = 0;
        for (int y = 0; y < h; ++y) {
            uchar* p = result.scanLine(y);
            for (int x = 0; x < w; ++x, p += 4, ++i) {
                const float line = lines[i];
                for (int c = 0; c < 3; ++c)
                    p[c] = uchar(p[c] + (255 - p[c]) * line);
            }
        }
        return result;
    };

    if (options.outline && !sink({withLines(), QStringLiteral("outline")}, index++, total))
        return;

    const QString regionTag = useColor ? QStringLiteral("color") : QStringLiteral("tone");
    for (size_t r = 0; r < regions.size(); ++r) {
        const Region& region = regions[r];
        size_t i = 0;
        for (int y = 0; y < h; ++y) {
            uchar* p = fill.scanLine(y);
            for (int x = 0; x < w; ++x, p += 4, ++i) {
                if (labels[i] == region.label) {
                    p[0] = region.display[0];
                    p[1] = region.display[1];
                    p[2] = region.display[2];
                }
            }
        }
        if (!sink({withLines(), regionTag + QString::number(r + 1)}, index++, total))
            return;
    }

    if (options.finalOriginal)
        sink({image, QStringLiteral("original")}, index++, total);
}

QString saveSteps(const QImage& source, const StepOptions& options, const QString& folder,
                  const StepProgress& progress)
{
    const bool folderExisted = QFileInfo::exists(folder);
    QDir dir(folder);
    if (!dir.mkpath(QStringLiteral(".")))
        return QCoreApplication::translate("lumi::StepGenerator", "Could not create %1")
            .arg(QDir::toNativeSeparators(folder));

    QStringList written;
    QString error;
    bool canceled = false;
    generateSteps(source, options, [&](const GeneratedStep& step, int index, int total) {
        const QString name = QStringLiteral("step%1_%2.png").arg(index + 1, 2, 10, QLatin1Char('0')).arg(step.name);
        written << name;
        if (!step.image.save(dir.filePath(name), "PNG")) {
            error = QCoreApplication::translate("lumi::StepGenerator", "Could not write %1")
                        .arg(QDir::toNativeSeparators(dir.filePath(name)));
            return false;
        }
        if (progress && !progress(index + 1, total)) {
            canceled = true;
            return false;
        }
        return true;
    });

    if (canceled || !error.isEmpty()) {
        for (const QString& name : std::as_const(written))
            dir.remove(name);
        if (!folderExisted)
            dir.rmdir(QStringLiteral("."));
    }
    return error;
}

} // namespace lumi
