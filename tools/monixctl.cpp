// monixctl — CLI to exercise/verify the Monix library against real hardware.
#include "monix.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
using namespace monix;

static const struct { const char* name; Master m; } MASTERS[] = {
    {"mono", Master::Mono}, {"phase", Master::Phase}, {"cut", Master::Cut},
    {"dim", Master::Dim}, {"talk", Master::Talkback}, {"alt", Master::Alt},
};

int main(int argc, char** argv) {
    Device d;
    if (!d.open()) { fprintf(stderr, "open failed: %s\n", d.lastError()); return 1; }

    if (argc < 2 || !strcmp(argv[1], "status")) {
        printf("Sample rate : %d Hz\n", d.getSampleRate());
        float mv; if (d.getMonitorVolume(mv)) printf("Monitor vol : %.2f\n", mv);
        printf("Masters     :");
        for (auto& x : MASTERS) { bool b=false; d.getMaster(x.m, b); printf(" %s=%d", x.name, b); }
        printf("\n");
    } else if (!strcmp(argv[1], "monitor") && argc == 3) {
        d.setMonitorVolume(atof(argv[2])); puts("ok");
    } else if (!strcmp(argv[1], "master") && argc == 4) {
        for (auto& x : MASTERS) if (!strcmp(argv[2], x.name)) { d.setMaster(x.m, !strcmp(argv[3], "on")); puts("ok"); return 0; }
        fprintf(stderr, "unknown master\n"); return 1;
    } else if (!strcmp(argv[1], "sr") && argc == 3) {
        d.setSampleRate(atoi(argv[2])); puts("ok");
    } else if (!strcmp(argv[1], "mixer") && argc == 4) {
        d.setMixerCell(atoi(argv[2]), atof(argv[3])); puts("ok");
    } else if (!strcmp(argv[1], "hp") && argc == 4) {
        d.setHeadphoneVolume(atoi(argv[2]), atof(argv[3])); puts("ok");
    } else if (!strcmp(argv[1], "meters")) {
        puts("Live meter indices (speak/play into one source; note which 'in#' lights up). Ctrl-C to stop.");
        for (;;) {
            uint8_t b0[32]={0}, b1[12]={0};
            d.readMeterBlock(0, b0, 32); d.readMeterBlock(1, b1, 12);
            printf("\rIN:");
            for (int i=0;i<16;i++){ int v=b0[i*2]|(b0[i*2+1]<<8); if (v>150) printf(" in%d=%d", i, v); }
            printf("  OUT:");
            for (int i=0;i<6;i++){ int v=b1[i*2]|(b1[i*2+1]<<8); if (v>150) printf(" out%d=%d", i, v); }
            printf("        "); fflush(stdout); usleep(150000);
        }
    } else if (!strcmp(argv[1], "route") && argc == 4) {
        d.setRouting(atoi(argv[2]), (uint8_t)strtol(argv[3], nullptr, 0)); puts("ok");
    } else if (!strcmp(argv[1], "restore-main")) {
        d.setOutputRouting(0, 1, Device::OutDest::Main); puts("outputs 1+2 -> Main Mix (stereo)");
    } else {
        fprintf(stderr, "usage: monixctl [status | monitor V | master <mono|phase|cut|dim|talk|alt> on/off | sr HZ | mixer CELL V | hp N V]\n");
        return 1;
    }
    return 0;
}
