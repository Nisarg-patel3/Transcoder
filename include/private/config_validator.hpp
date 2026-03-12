#pragma once

#include "transcoder.hpp"
#include "pipeline_context.hpp"

#include <string>

namespace transcoder
{
    // ─────────────────────────────────────────────────────────────────────────────
    // ConfigValidator
    // ─────────────────────────────────────────────────────────────────────────────

    class ConfigValidator
    {
    public:
        // -------------------------------------------------------------------------
        // validate() — checks every field in one pass, prints all errors/warnings.
        // -------------------------------------------------------------------------
        static bool validateInputOutputPaths(const PipelineConfig &cfg);
        static bool validateConfiguration(PipelineConfig &cfg, PipelineContext& pipelineCtx);

    private:
        // ─────────────────────────────────────────────────────────────────────────
        // Video ↔ audio cross-compatibility rules
        // These apply on top of the per-container checks above.
        // ─────────────────────────────────────────────────────────────────────────
        static bool checkCompat(const PipelineConfig &cfg, ContainerFormat container, InputInfo inputInfo);        
    };

} // namespace vtpl