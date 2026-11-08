#pragma once

#include <QImage>
#include <QString>

#include <functional>

namespace lumi {

struct StepOptions {
    enum class Method {
        Colors, // regions of similar colour, shown in their colour
        Tones,  // regions of similar brightness, shown in grey
    };

    Method method = Method::Colors;
    int levels = 5;           // regions to separate; a simple image may yield fewer
    bool darkFirst = true;    // paint dark regions first, or light regions first
    bool outline = true;      // start with a line drawing
    int lineDetail = 50;      // 0 (main shapes only) .. 100 (fine detail)
    bool finalOriginal = true; // end with the source image for reference
    int maxSide = 4096;       // working resolution; results scale with it
};

struct GeneratedStep {
    QImage image;  // RGBA8888, opaque, light on black: only what is lit gets projected
    QString name;  // short ASCII tag for the file name
};

// Receives each step as it is produced, so a large sequence never sits in memory
// as a whole. index is 0-based. Return false to stop generating.
using StepSink = std::function<bool(const GeneratedStep& step, int index, int total)>;

// Splits a finished image into cumulative steps: optional outline, then one more
// region per step, then optionally the source itself. Deterministic: the same
// input and options give the same steps. Safe to call from any thread.
void generateSteps(const QImage& source, const StepOptions& options, const StepSink& sink);

// Called after each saved step; return false to cancel.
using StepProgress = std::function<bool(int done, int total)>;

// Writes the steps as step01_outline.png, step02_color1.png, ... in folder, creating
// it if needed. On cancel or failure removes only what it wrote. Returns an error
// message, empty on success or cancel.
QString saveSteps(const QImage& source, const StepOptions& options, const QString& folder,
                  const StepProgress& progress);

} // namespace lumi
