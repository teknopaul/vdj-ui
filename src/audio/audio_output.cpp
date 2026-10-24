#include "audio_output.h"
#include <pulse/pulseaudio.h>

namespace vdj {

namespace {

struct Query {
    pa_mainloop* loop = nullptr;
    QList<AudioDevice> devices;
    bool done = false;
    bool failed = false;
};

void sinkInfo(pa_context*, const pa_sink_info* info, int eol, void* userdata)
{
    auto* q = static_cast<Query*>(userdata);
    if (eol > 0) {
        q->done = true;
        pa_mainloop_quit(q->loop, 0);
        return;
    }
    if (info)
        q->devices.append({ QString::fromUtf8(info->name), QString::fromUtf8(info->description) });
}

void contextState(pa_context* ctx, void* userdata)
{
    auto* q = static_cast<Query*>(userdata);
    switch (pa_context_get_state(ctx)) {
        case PA_CONTEXT_READY:
            pa_operation_unref(pa_context_get_sink_info_list(ctx, sinkInfo, q));
            break;
        case PA_CONTEXT_FAILED:
        case PA_CONTEXT_TERMINATED:
            q->failed = true;
            pa_mainloop_quit(q->loop, 1);
            break;
        default:
            break;
    }
}

} // namespace

QList<AudioDevice> listAudioDevices()
{
    QList<AudioDevice> result;
    result.append({ QString(), QStringLiteral("Default") });

    Query q;
    q.loop = pa_mainloop_new();
    if (!q.loop)
        return result;
    pa_context* ctx = pa_context_new(pa_mainloop_get_api(q.loop), "vdj-ui");
    if (ctx) {
        pa_context_set_state_callback(ctx, contextState, &q);
        if (pa_context_connect(ctx, nullptr, PA_CONTEXT_NOFLAGS, nullptr) >= 0) {
            int retval = 0;
            pa_mainloop_run(q.loop, &retval);
        }
        pa_context_disconnect(ctx);
        pa_context_unref(ctx);
    }
    pa_mainloop_free(q.loop);
    if (!q.failed)
        result.append(q.devices);
    return result;
}

} // namespace vdj
