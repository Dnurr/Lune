#pragma once

#include <QStringList>

// Rendering from the command line, without the editor window:
//
//   Lune.exe --render scene.lune [image.png]
//
// The scene's final image (what F4 writes) at the frame saved with the scene: from its scene
// camera (the saved viewport camera without one), at the size of its Render Settings (1920 x
// 1080 when they follow the viewport), with its exposure, tonemapper and the viewport's Sky
// and Post toggles. Without a file name it goes where F4 would put it.
//
// `args`: what follows "--render". Returns the process exit code (0: written).
int renderFromCommandLine(const QStringList &args);
