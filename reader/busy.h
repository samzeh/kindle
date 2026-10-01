// A full-screen message with an optional progress bar, for the few seconds a
// book takes to prepare ("Preparing book... 40%").
//
// Each call refreshes the e-paper (about 0.8 s on the device), so callers
// should only call it a handful of times per task.
#pragma once
#include <stdint.h>

// percent < 0 draws no bar.
void busyShow(const char *title, const char *message, int percent);
