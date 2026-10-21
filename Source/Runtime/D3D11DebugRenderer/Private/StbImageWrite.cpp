// StbImageWrite.cpp - the one TU that compiles stb_image_write (stb @ a7d4c217, PT::Stb) for the viewer's PNG captures.
#include "CoreMinimal.h"

#include "Misc/PtThirdPartyIncludesStart.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO   // the viewer writes the encoded bytes itself (wide paths, D3D11RHI.cpp)
#include <stb_image_write.h>
#include "Misc/PtThirdPartyIncludesEnd.h"
