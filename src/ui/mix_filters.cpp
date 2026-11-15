#include "mix_filters.h"

#include "audio/ladspa_host.h"

#include <memory>

using namespace trakcrate;

std::vector<MixProcessor> buildLadspaProcessors(const std::vector<MixFilter>& filters, MixFilterReport* report)
{
    std::vector<MixProcessor> out;
    for (const MixFilter& f : filters) {
        if (f.kind != MixFilterKind::Ladspa || !f.on) continue;
        vdj::LadspaPluginInfo info;
        if (!vdj::LadspaHost::find(QString::fromStdString(f.plugin), &info)) {
            if (report) report->skipped.push_back(QString::fromStdString(f.plugin) + ": plugin not found");
            continue;
        }
        std::shared_ptr<vdj::LadspaInstance> inst = vdj::LadspaInstance::create(info, vdj::LadspaInstance::kRate);
        if (!inst) {
            if (report) report->skipped.push_back(QString::fromStdString(f.plugin) + ": cannot be loaded");
            continue;
        }
        for (size_t i = 0; i < info.controls.size(); ++i) {
            auto it = f.params.find(info.controls[i].name.toStdString());
            inst->setControl(int(i), it != f.params.end() ? float(it->second) : info.controls[i].def);
        }
        out.push_back([inst](float* stereo, int frames) { inst->process(stereo, frames); });
    }
    return out;
}
