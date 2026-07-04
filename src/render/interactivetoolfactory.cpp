#include "interactivetool.h"

#include "tools/rubberbandselecttool.h"
#include "tools/selectlayertool.h"

std::vector<std::unique_ptr<InteractiveTool>> createBuiltinInteractiveTools()
{
    std::vector<std::unique_ptr<InteractiveTool>> tools;
    tools.push_back(std::make_unique<SelectLayerTool>());
    tools.push_back(std::make_unique<RubberBandSelectTool>());
    return tools;
}
