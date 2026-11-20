#pragma once

#include <rhi/qshader.h>

// SPIRV-Cross renames uniform block members named like GLSL built-in functions in the GLSL it
// writes (a member `step` becomes `_step`), but the shader's reflection keeps the original
// name. Qt's OpenGL backend sets uniform blocks member by member, looking each one up by its
// reflected name: a renamed member is never found and reads as 0. Struct members don't clash
// with functions, so this puts the reflected names back in every GLSL variant of `shader`
// (other variants are left as they are). Every shader the renderer loads or bakes goes
// through it.
QShader withReflectedMemberNames(QShader shader);
