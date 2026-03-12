#pragma once

#include <string>
#include <memory>

extern "C"
{
#include <libavutil/frame.h>
}

namespace transcoder {

class ProcessingStage {
public:
    virtual ~ProcessingStage() = default;

    virtual bool initialize() = 0;

    virtual AVFrame* process(AVFrame* frame) = 0;

    virtual std::string getName() const = 0;
};

// ─────────────────────────────────────────────────────────────
// PassThroughProcessor — no processing, just passes frames through
// ─────────────────────────────────────────────────────────────
class PassThroughProcessor : public ProcessingStage {
public:
    bool        initialize() override { return true; }
    AVFrame*    process(AVFrame* frame) override { return frame; }
    std::string getName() const override { return "PassThrough"; }
};

} // namespace transcoder
