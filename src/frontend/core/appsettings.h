#pragma once

#include <QString>
#include <rhi/qrhi.h>
#include <vector>

// Application preferences (Edit > Preferences), kept with QSettings: per user, not per scene.
namespace AppSettings
{
// The graphics backends this build can use, in the order the Preferences list them.
std::vector<QRhi::Implementation> availableBackends();
// "Direct3D 11", "Vulkan"...
QString backendLabel(QRhi::Implementation backend);

// The backend the editor (and command-line renders) start with: the saved one if this build
// has it, else the platform's default (Direct3D 11 on Windows). A change applies on restart.
QRhi::Implementation backend();
void setBackend(QRhi::Implementation backend);

// GPU memory each fluid volume's cache may take (cached frames and snapshots), in megabytes.
constexpr int MinFluidCacheMegabytes = 256;
constexpr int MaxFluidCacheMegabytes = 65536;
int fluidCacheMegabytes(); // default 2048
void setFluidCacheMegabytes(int megabytes);
} // namespace AppSettings
