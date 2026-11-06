// Implementación de stb_image (solo lectura de imágenes). Sin dependencias de Geode.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO          // se lee siempre desde memoria (bytes leídos con geode::utils::file)
#include "stb_image.h"
