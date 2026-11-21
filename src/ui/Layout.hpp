// ============================================================================
//  Geometria de la ventana del importador (sin dependencias de Geode: la usan GifPopup::init y tests/layout_test.cpp).
//  Unidades de la ventana: X desde la izquierda, Y desde ARRIBA (igual que put(x, y) en GifPopup::init y que el volcado de UI).
//
//      +--------------------------------------------------------------+  <- 0
//      | (X)            GIF Importer (beta)                      (i)  |  titulo y boton de info (esquina superior derecha)
//      |  +--------+   <  nombre del metodo  >                        |
//      |  |        |      descripcion (caja de 248 x 34)              |
//      |  | vista  |   Objetos por frame ....................  1500   |
//      |  | previa |   [=========o======]                             |
//      |  +--------+   ...                                            |
//      |  estado       [ ] Escala de grises   [x] Canales             |
//      |  nota                                                        |
//      |  [Ajustes][UI]                          [ GIF ] [ Colocar ]  |  <- una sola fila: misma Y, misma altura, misma escala de texto
//      +--------------------------------------------------------------+  <- 290
//       14                168                              416   430
// ============================================================================
#pragma once

namespace gdx::layout {

constexpr float kW = 430.f, kH = 290.f;
constexpr float kMargin = 14.f;                                   // margen izquierdo = margen derecho

// columna izquierda: vista previa (cuadrada) + estado + nota
constexpr float kPrevSize = 140.f;
constexpr float kLeftL = kMargin, kLeftR = kMargin + kPrevSize;   // 14 .. 154
constexpr float kLeftCx = (kLeftL + kLeftR) / 2.f;                // 84
constexpr float kPrevCy = 112.f;                                  // la vista previa ocupa y = 42 .. 182
constexpr float kStatACy = 194.f, kStatBCy = 209.f;              // 15 de separacion: las cajas de los dos textos (15,3 y 13,5 de alto) no se pisan
constexpr float kNoteCy = 229.f, kNoteMaxW = kPrevSize + 10.f, kNoteMaxH = 22.f;   // caja de la nota: y = 218 .. 240

// columna derecha
constexpr float kRightL = 168.f, kRightR = kW - kMargin;          // 168 .. 416
constexpr float kRightCx = (kRightL + kRightR) / 2.f;             // 292
constexpr float kArrowW = 16.5f;                                  // ancho real de la flecha (GJ_arrow_03 a escala 0.5)
constexpr float kArrowCy = 52.f;
constexpr float kArrowCxL = kRightL + kArrowW / 2.f, kArrowCxR = kRightR - kArrowW / 2.f;   // las flechas llegan justo a los bordes de la columna
constexpr float kTitleGap = 6.f;                                  // aire entre el nombre del metodo y cada flecha
constexpr float kTitleMaxW = (kArrowCxR - kArrowCxL) - kArrowW - 2.f * kTitleGap;           // = 203,5
constexpr float kDescTop = 64.f, kDescBottom = 98.f;              // caja de la descripcion: entre el nombre del metodo y "Objetos por frame"
constexpr float kDescCy = (kDescTop + kDescBottom) / 2.f;
constexpr float kDescMaxW = kRightR - kRightL, kDescMaxH = kDescBottom - kDescTop;          // 248 x 34

// fila de botones de abajo: TODOS con la misma Y, la misma altura y la misma escala de texto
constexpr float kBtnRowY = 262.f;                                 // centro; con 39 de alto ocupa y = 242,5 .. 281,5 (8,5 de margen abajo)
constexpr float kBtnH = 39.f;
constexpr float kBtnGap = 8.f;
constexpr float kBtnTextScale = 0.6f;

// boton de info: esquina superior derecha, dentro de la ventana, a la misma distancia del borde que el de cerrar por el otro lado
constexpr float kInfoSize = 23.f;
constexpr float kInfoCx = kW - 17.f, kInfoCy = 17.f;

// el boton de la fila se coloca por su borde (el ancho real lo da el ButtonSprite)
constexpr float centerFromLeft(float left, float width) { return left + width / 2.f; }
constexpr float centerFromRight(float right, float width) { return right - width / 2.f; }

} // namespace gdx::layout
