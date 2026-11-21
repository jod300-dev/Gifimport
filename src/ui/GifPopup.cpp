#include "GifPopup.hpp"

#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/async.hpp>
#include <thread>
#include <chrono>
#include <ctime>
#include <sstream>
#include <filesystem>
#include <fstream>
#include "diag.hpp"
#include "UiCollect.hpp"      // volcado de la interfaz (ajuste "ui-dump")
#include "Layout.hpp"         // geometria de la ventana (la misma que prueba tests/layout_test.cpp)
#include "FitText.hpp"        // encaje de texto en una caja
#include <cctype>

namespace wlay = gdx::layout;

namespace {
    constexpr char const* kModVersion = "v0.5.12-beta";
    constexpr float kPopupW = wlay::kW, kPopupH = wlay::kH;
    constexpr float kPrevSize = wlay::kPrevSize;
    constexpr int kMethodCount = int(gdx::Method::Ascii);          // metodos 1-17 (el ASCII no sirve para animar)
    constexpr int kMethodTotal = kMethodCount + 2 + 10;            // + metodo 18 (capas), 19 (arbol de deltas) y 20-29 (capas LSA)
    constexpr int kMinPF = 10, kMaxPF = 5000;                      // objetos por frame
    constexpr int kMinFrames = 2, kMaxFrames = 300;
    constexpr int kMinSize = 150, kMaxSize = 6000;

    int qualityFromSetting() {
        auto q = Mod::get()->getSettingValue<std::string>("quality");
        if (q == "Normal") return 1;
        if (q == "Alto") return 2;
        if (q == "Maximo") return 3;
        return 0;                                                  // Rapido (por defecto: hay muchos frames)
    }
    // Duracion de calculo de los metodos 18-29 (0 Rapido, 1 Normal = como antes, 2 Alto, 3 Maximo)
    int effortFromSetting() {
        auto q = Mod::get()->getSettingValue<std::string>("compute-effort");
        if (q == "Rapido") return 0;
        if (q == "Alto") return 2;
        if (q == "Maximo") return 3;
        if (q == "Extremo") return 4;
        if (q == "Ultra") return 5;
        return 1;
    }
    using gdx::fit::fitWrapped;                                       // texto dentro de una caja (nada se sale ni se pisa): ver FitText.hpp
    // Avisos: los cortos en un FLAlertLayer; los largos (errores con mucho texto) en una ventana con SCROLL y boton OK siempre visible.
    void showAlert(char const* title, std::string const& text) {
        if (text.size() <= 180) FLAlertLayer::create(title, text.c_str(), "OK")->show();
        else MDPopup::create(title, text, "OK")->show();
    }

    std::string platformString() {
        std::string r = "plataforma: ";
#ifdef GEODE_PLATFORM_NAME
        r += GEODE_PLATFORM_NAME;
#else
        r += "?";
#endif
#ifdef GEODE_GD_VERSION_STRING
        r += std::string("   GD ") + GEODE_GD_VERSION_STRING;
#endif
        return r;
    }
    void nowStrings(std::string& stamp, std::string& when) {
        const std::time_t now = std::time(nullptr); std::tm lt{};
#ifdef _WIN32
        localtime_s(&lt, &now);
#else
        localtime_r(&now, &lt);
#endif
        char a[32], b[48];
        std::strftime(a, sizeof a, "%Y%m%d_%H%M%S", &lt);
        std::strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S", &lt);
        stamp = a; when = b;
    }
    // sliders 0..1 <-> valor
    float pfToSlider(int v) { return std::clamp(float(std::log(double(v) / kMinPF) / std::log(double(kMaxPF) / kMinPF)), 0.f, 1.f); }
    int sliderToPf(float s) {
        double u = kMinPF * std::pow(double(kMaxPF) / kMinPF, std::clamp(s, 0.f, 1.f));
        int step = u < 100 ? 5 : u < 500 ? 10 : u < 2000 ? 50 : 100;
        return std::clamp(int(std::lround(u / step)) * step, kMinPF, kMaxPF);
    }
    float framesToSlider(int v) { return std::clamp(float(v - kMinFrames) / float(kMaxFrames - kMinFrames), 0.f, 1.f); }
    int sliderToFrames(float s) { return kMinFrames + int(std::lround(std::clamp(s, 0.f, 1.f) * (kMaxFrames - kMinFrames))); }
    float sizeToSlider(int u) { return std::clamp(float(std::log(double(u) / kMinSize) / std::log(double(kMaxSize) / kMinSize)), 0.f, 1.f); }
    int sliderToSize(float v) {
        double u = kMinSize * std::pow(double(kMaxSize) / kMinSize, std::clamp(v, 0.f, 1.f));
        int step = u < 600 ? 10 : u < 2000 ? 50 : 100;
        return std::clamp(int(std::lround(u / step)) * step, kMinSize, kMaxSize);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Colocacion POR TANDAS. Crear 100k-200k objetos de golpe con createObjectsFromString congela el juego (o lo cierra en moviles) y,
// si se pasa el limite de objetos del editor, GD corta la cadena y se pierden los ultimos objetos. Aqui la cadena se parte en trozos
// de ~1500 objetos (cortando siempre en ';') y se crean unos pocos por fotograma (~25 ms), con aviso de progreso.
// El nodo cuelga del editor: si se sale del editor, la colocacion se cancela sola.
// ---------------------------------------------------------------------------------------------------------------------
namespace {
    std::vector<std::string> splitChain(std::string const& s, size_t perPart) {
        std::vector<std::string> parts;
        size_t start = 0, count = 0;
        for (size_t i = 0; i <= s.size(); ++i) {
            if (i == s.size() || s[i] == ';') {
                if (i > start) ++count;
                if (count >= perPart || i == s.size()) {
                    if (i > start || !parts.empty()) {
                        size_t endPos = i;
                        while (endPos > start && s[endPos - 1] == ';') --endPos;
                        if (endPos > start) parts.push_back(s.substr(start, endPos - start));
                    }
                    start = i + 1; count = 0;
                }
            }
        }
        return parts;
    }

    class PlaceRunner : public CCNode {
    public:
        std::vector<std::string> parts;
        size_t next = 0, placed = 0, expected = 0, totalObjs = 0;
        bool selectAfter = false;
        std::string doneMsg;
        Ref<CCLabelBMFont> label;                                   // progreso (propio, sin depender de la API de Notification)
        CCArray* selected = nullptr;

        static PlaceRunner* create() {
            auto r = new PlaceRunner();
            if (r && r->init()) { r->autorelease(); return r; }
            delete r; return nullptr;
        }
        ~PlaceRunner() {
            if (selected) selected->release();
            if (label && label->getParent()) label->removeFromParent();
        }

        void begin(std::vector<std::string> p, size_t total, bool select, std::string const& msg) {
            parts = std::move(p); totalObjs = total; doneMsg = msg;
            selectAfter = select && total <= 8000;                    // seleccionar decenas de miles de objetos tambien cuelga el editor
            if (selectAfter) { selected = CCArray::create(); selected->retain(); }
            if (auto scene = CCDirector::sharedDirector()->getRunningScene()) {
                label = CCLabelBMFont::create("Colocando objetos... 0%", "bigFont.fnt");
                auto ws = CCDirector::sharedDirector()->getWinSize();
                label->setScale(0.55f);
                label->setPosition({ws.width / 2.f, ws.height - 28.f});
                scene->addChild(label, 9999);
            }
            this->schedule(schedule_selector(PlaceRunner::step));
        }

        void step(float) {
            auto lel = LevelEditorLayer::get();
            if (!lel) { this->finish(false); return; }
            auto t0 = std::chrono::steady_clock::now();
            do {
                if (next >= parts.size()) break;
                {   // objetos que ESTA tanda deberia crear (segmentos separados por ';'): si GD crea menos se avisa al final
                    size_t segs = parts[next].empty() ? 0 : 1;
                    for (char ch : parts[next]) if (ch == ';') ++segs;
                    expected += segs;
                }
                CCArray* made = lel->createObjectsFromString(parts[next].c_str(), true, true);
                if (made) {
                    placed += made->count();
                    if (selected) for (auto* o : CCArrayExt<GameObject*>(made)) selected->addObject(o);
                }
                std::string().swap(parts[next]);
                ++next;
            } while (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() < 25.0);
            if (next >= parts.size()) { this->finish(true); return; }
            char buf[96];
            std::snprintf(buf, sizeof buf, "Colocando objetos... %d%%", int(100.0 * double(next) / double(std::max<size_t>(1, parts.size()))));
            if (label) label->setString(buf);
        }

        void finish(bool ok) {
            this->unschedule(schedule_selector(PlaceRunner::step));
            if (ok && selected && selected->count() > 0) {
                if (auto* ed = EditorUI::get()) ed->selectObjects(selected, true);
            }
            if (label && label->getParent()) label->removeFromParent();
            std::string msg = ok ? doneMsg : std::string("Colocacion cancelada (se salio del editor)");
            bool warn = false;
            if (ok && placed < expected) {                       // GD no creo todo lo que se le pidio: la animacion saldria con huecos
                msg += " | AVISO: GD creo " + std::to_string(placed) + " de " + std::to_string(expected) + " objetos";
                warn = true;
            }
            Notification::create(msg, ok && !warn ? NotificationIcon::Success : NotificationIcon::Error, warn ? 8.f : 3.5f)->show();
            this->removeFromParent();
        }
    };
}

GifPopup* GifPopup::create(EditorUI* editor) {
    auto ret = new GifPopup();
    if (ret->init(editor)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

GifPopup::~GifPopup() {
    if (m_job) m_job->prog.cancel.store(true);
    if (m_build) m_build->prog.cancel.store(true);
}

bool GifPopup::init(EditorUI* editor) {
    if (!Popup::init(kPopupW, kPopupH)) return false;
    m_editor = editor;
    this->setTitle("GIF Importer (beta)");

    auto* mod = Mod::get();
    m_method = std::clamp(mod->getSavedValue<int>("method", 2), 0, kMethodTotal - 1);
    m_perFrame = std::clamp(mod->getSavedValue<int>("per-frame", 300), kMinPF, kMaxPF);
    m_sizeU = std::clamp(mod->getSavedValue<int>("size-u", 600), kMinSize, kMaxSize);
    m_maxFrames = std::clamp(mod->getSavedValue<int>("max-frames", 40), kMinFrames, kMaxFrames);
    m_gray = mod->getSavedValue<bool>("gray", false);
    m_chan = mod->getSavedValue<bool>("chan", false);                // HSV por defecto

    auto put = [&](CCNode* n, float x, float y) { m_mainLayer->addChildAtPosition(n, Anchor::TopLeft, ccp(x, -y)); };
    auto putBtn = [&](CCNode* n, float x, float y) { m_buttonMenu->addChildAtPosition(n, Anchor::TopLeft, ccp(x, -y)); };
    auto label = [&](char const* text, char const* font, float scale, float x, float y, float ax = 0.5f) {
        auto l = CCLabelBMFont::create(text, font);
        l->setScale(scale);
        l->setAnchorPoint({ax, 0.5f});
        put(l, x, y);
        return l;
    };

    // ============ izquierda: vista previa y estado ============
    m_previewHolder = CCNode::create();
    m_previewHolder->setContentSize({kPrevSize, kPrevSize});
    m_previewHolder->setAnchorPoint({0.5f, 0.5f});
    m_previewHolder->addChild(CCLayerColor::create(ccc4(0, 0, 0, 120), kPrevSize, kPrevSize));
    auto hint = CCLabelBMFont::create("Elige un GIF", "chatFont.fnt");
    hint->setOpacity(120);
    hint->setPosition({kPrevSize / 2, kPrevSize / 2});
    m_previewHolder->addChild(hint);
    put(m_previewHolder, wlay::kLeftCx, wlay::kPrevCy);

    m_statA = label("Elige un GIF animado", "chatFont.fnt", 0.85f, wlay::kLeftCx, wlay::kStatACy);
    m_statB = label("", "chatFont.fnt", 0.75f, wlay::kLeftCx, wlay::kStatBCy);
    m_noteLabel = CCLabelBMFont::create("", "chatFont.fnt", wlay::kNoteMaxW, kCCTextAlignmentCenter);
    m_noteLabel->setScale(0.6f);
    m_noteLabel->setColor({255, 200, 80});
    put(m_noteLabel, wlay::kLeftCx, wlay::kNoteCy);                     // la nota se encaja en una caja de 150 x 22 (ver setStatus): no llega a los botones

    // ============ derecha ============
    auto arrow = [&](bool flip, SEL_MenuHandler cb, float x, float y) {
        auto spr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
        spr->setScale(0.5f);
        spr->setFlipX(flip);
        putBtn(CCMenuItemSpriteExtra::create(spr, this, cb), x, y);
    };
    arrow(false, menu_selector(GifPopup::onPrev), wlay::kArrowCxL, wlay::kArrowCy);
    arrow(true,  menu_selector(GifPopup::onNext), wlay::kArrowCxR, wlay::kArrowCy);
    m_methodLabel = label("", "bigFont.fnt", 0.5f, wlay::kRightCx, wlay::kArrowCy);
    m_descLabel = CCLabelBMFont::create("", "chatFont.fnt", wlay::kDescMaxW, kCCTextAlignmentCenter);
    m_descLabel->setScale(0.6f);
    put(m_descLabel, wlay::kRightCx, wlay::kDescCy);                    // se encaja en la caja de 248 x 34 entre el nombre del metodo y los sliders (ver refreshLabels)

    label("Objetos por frame", "bigFont.fnt", 0.34f, wlay::kRightL, 108.f, 0.f);
    m_perFrameValue = label("", "bigFont.fnt", 0.34f, wlay::kRightR, 108.f, 1.f);
    m_perFrameSlider = Slider::create(this, menu_selector(GifPopup::onPerFrameSlider), 0.78f);
    m_perFrameSlider->setValue(pfToSlider(m_perFrame));
    put(m_perFrameSlider, wlay::kRightCx, 124.f);

    label("Lado largo", "bigFont.fnt", 0.34f, wlay::kRightL, 146.f, 0.f);
    m_sizeValue = label("", "bigFont.fnt", 0.34f, wlay::kRightR, 146.f, 1.f);
    m_sizeSlider = Slider::create(this, menu_selector(GifPopup::onSizeSlider), 0.78f);
    m_sizeSlider->setValue(sizeToSlider(m_sizeU));
    put(m_sizeSlider, wlay::kRightCx, 162.f);

    label("Frames maximos", "bigFont.fnt", 0.34f, wlay::kRightL, 184.f, 0.f);
    m_framesValue = label("", "bigFont.fnt", 0.34f, wlay::kRightR, 184.f, 1.f);
    m_framesSlider = Slider::create(this, menu_selector(GifPopup::onFramesSlider), 0.78f);
    m_framesSlider->setValue(framesToSlider(m_maxFrames));
    put(m_framesSlider, wlay::kRightCx, 200.f);

    m_chkGray = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(GifPopup::onGray), 0.5f);
    m_chkGray->toggle(m_gray);
    putBtn(m_chkGray, 174.f, 224.f);
    label("Escala de grises", "bigFont.fnt", 0.28f, 186.f, 224.f, 0.f);
    m_chkChan = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(GifPopup::onChan), 0.5f);
    m_chkChan->toggle(m_chan);
    putBtn(m_chkChan, 300.f, 224.f);
    if (auto* l = label("Canales (sin HSV)", "bigFont.fnt", 0.28f, 312.f, 224.f, 0.f)) l->limitLabelWidth(106.f, 0.28f, 0.15f);

    // ---- fila de botones: UNA sola fila (misma Y, misma altura de 39, misma escala de texto). Ajustes y UI pegados al borde izquierdo de la vista previa;
    //      GIF y Colocar pegados al borde derecho de la columna derecha. El ancho real de cada boton lo da su ButtonSprite.
    auto mkBtn = [&](char const* text, char const* frame, SEL_MenuHandler cb) {
        return CCMenuItemSpriteExtra::create(ButtonSprite::create(text, "bigFont.fnt", frame, wlay::kBtnTextScale), this, cb);
    };
    auto putLeft = [&](CCMenuItemSpriteExtra* b, float left) {            // devuelve el borde derecho del boton
        const float w = b->getContentSize().width;
        putBtn(b, wlay::centerFromLeft(left, w), wlay::kBtnRowY);
        return left + w;
    };
    auto putRight = [&](CCMenuItemSpriteExtra* b, float right) {          // devuelve el borde izquierdo del boton
        const float w = b->getContentSize().width;
        putBtn(b, wlay::centerFromRight(right, w), wlay::kBtnRowY);
        return right - w;
    };
    auto setBtn = mkBtn("Ajustes", "GJ_button_04.png", menu_selector(GifPopup::onSettings));
    m_dumpBtn = mkBtn("UI", "GJ_button_05.png", menu_selector(GifPopup::onDumpUi));
    m_dumpBtn->setID("ui-dump-button"_spr);
    putLeft(m_dumpBtn, putLeft(setBtn, wlay::kLeftL) + wlay::kBtnGap);
    auto pickBtn = mkBtn("GIF", "GJ_button_01.png", menu_selector(GifPopup::onPick));
    auto placeBtn = mkBtn("Colocar", "GJ_button_02.png", menu_selector(GifPopup::onPlace));
    putRight(pickBtn, putRight(placeBtn, wlay::kRightR) - wlay::kBtnGap);

    // ---- boton de info: esquina superior derecha (la X de cerrar esta en la superior izquierda)
    auto infoBtn = CCMenuItemSpriteExtra::create(CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png"), this, menu_selector(GifPopup::onInfo));
    putBtn(infoBtn, wlay::kInfoCx, wlay::kInfoCy);
    this->refreshDumpButton();                                            // solo se ve con el ajuste "Guardar volcado de la interfaz"
    this->scheduleAutoDump("abrir", 1.5f);                                // (solo con "ui-dump-auto") espera a que termine la animacion de apertura

    this->schedule(schedule_selector(GifPopup::tick), 0.1f);
    this->refreshLabels();
    return true;
}

// ------------------------------------------------------------- etiquetas ---

void GifPopup::refreshLabels() {
    std::string name, desc;
    if (layered()) {
        if (lsaKind() > 0) { name = gdx::lsa::methodName(lsaKind()); desc = gdx::lsa::methodDesc(lsaKind()); }
        else { name = treeMode() ? gdx::treeName() : gdx::layeredName(); desc = treeMode() ? gdx::treeDesc() : gdx::layeredDesc(); }
    } else {
        auto const& info = gdx::methodInfo(method());
        name = info.name; desc = info.desc;
    }
    m_methodLabel->setString(name.c_str());
    m_methodLabel->limitLabelWidth(wlay::kTitleMaxW, 0.5f, 0.2f);    // con aire a los dos lados: no toca las flechas
    if (desc != m_lastDesc) {                                     // solo si cambia el metodo (refreshLabels tambien corre al mover los sliders)
        m_lastDesc = desc;
        fitWrapped(m_descLabel, desc, wlay::kDescMaxW, wlay::kDescMaxH, 0.6f, 0.3f);
    }

    char buf[64];
    {   // si el "Tope total de objetos" recorta el presupuesto, se muestra el valor que de verdad se aplica
        int eff = m_gif ? effectivePerFrame(makePlan().count()) : m_perFrame;
        if (eff < m_perFrame) std::snprintf(buf, sizeof buf, "%d (tope: %d)", m_perFrame, eff);
        else std::snprintf(buf, sizeof buf, "%d", m_perFrame);
    }
    m_perFrameValue->setString(buf);
    std::snprintf(buf, sizeof buf, "%d u (%.1f bloques)", m_sizeU, m_sizeU / 30.0);
    m_sizeValue->setString(buf);
    if (m_gif && int(m_gif->frames.size()) < m_maxFrames) std::snprintf(buf, sizeof buf, "%d (el GIF tiene %d)", m_maxFrames, int(m_gif->frames.size()));
    else std::snprintf(buf, sizeof buf, "%d", m_maxFrames);
    m_framesValue->setString(buf);

    auto* mod = Mod::get();
    mod->setSavedValue<int>("method", m_method);
    mod->setSavedValue<int>("per-frame", m_perFrame);
    mod->setSavedValue<int>("size-u", m_sizeU);
    mod->setSavedValue<int>("max-frames", m_maxFrames);
    mod->setSavedValue<bool>("gray", m_gray);
    mod->setSavedValue<bool>("chan", m_chan);
}

void GifPopup::setStatus(std::string const& a, std::string const& b, std::string const& note) {
    m_statA->setString(a.c_str());
    m_statA->limitLabelWidth(150.f, 0.85f, 0.4f);
    m_statB->setString(b.c_str());
    m_statB->limitLabelWidth(150.f, 0.75f, 0.4f);
    fitWrapped(m_noteLabel, note, wlay::kNoteMaxW, wlay::kNoteMaxH, 0.6f, 0.4f);
    this->scheduleAutoDump("estado", 1.0f);
}

void GifPopup::stepMethod(int dir) {
    m_method = (m_method + dir + kMethodTotal) % kMethodTotal;
    this->refreshLabels();
    this->scheduleRecompute();
}

void GifPopup::onPerFrameSlider(CCObject* sender) {
    int v = sliderToPf(static_cast<SliderThumb*>(sender)->getValue());
    if (v == m_perFrame) return;
    m_perFrame = v;
    this->refreshLabels();
    this->scheduleRecompute();
}

void GifPopup::onSizeSlider(CCObject* sender) {
    m_sizeU = sliderToSize(static_cast<SliderThumb*>(sender)->getValue());
    this->refreshLabels();                      // solo afecta a la colocacion
}

void GifPopup::onFramesSlider(CCObject* sender) {
    m_maxFrames = sliderToFrames(static_cast<SliderThumb*>(sender)->getValue());
    this->refreshLabels();                      // no cambia el frame 0: solo el reparto al colocar
    if (layered()) { this->scheduleRecompute(); return; }       // el metodo 18 reparte las capas segun los frames
    if (m_hasResult && !m_build) this->applyPreview(nullptr);   // refresca el texto de estado
}

void GifPopup::onGray(CCObject*) {
    m_gray = !m_gray;                           // en el callback el toggler aun no ha cambiado su estado
    this->refreshLabels();
    this->scheduleRecompute();
}

// Check "Canales (sin HSV)": los objetos usan solo canales de color (2..), nunca el 1, con Color triggers al inicio.
void GifPopup::onChan(CCObject*) {
    m_chan = !m_chan;                           // en el callback el toggler aun no ha cambiado su estado
    this->refreshLabels();
    if (layered()) this->scheduleRecompute();   // 18/19: las capas se recalculan con la paleta
    else if (m_hasResult && !m_build) this->applyPreview(nullptr);
}

int GifPopup::paletteSize() const {
    return std::clamp(int(Mod::get()->getSettingValue<int>("layered-palette-size")), 8, 900);
}

void GifPopup::onSettings(CCObject*) {
    openSettingsPopup(Mod::get());
}

// ------------------------------------------------------------ cargar GIF ----

void GifPopup::onPick(CCObject*) {
    if (m_build) return;
    auto options = file::FilePickOptions{
        std::nullopt,
        {{"GIF animado", {"*.gif"}}}
    };
    async::spawn(
        file::pick(file::PickMode::OpenFile, options),
        [weak = WeakRef<GifPopup>(this)](auto result) {
            auto self = weak.lock();
            if (!self) return;
            if (!result.isOk()) {
                self->setStatus("No se pudo abrir", "el selector de archivos");
                return;
            }
            auto opt = result.unwrap();
            if (!opt) return;                                    // cancelado
            auto bytes = utils::file::readBinary(opt.value());
            if (!bytes.isOk()) {
                self->setStatus("No se pudo leer", "el archivo");
                return;
            }
            self->loadGifBytes(bytes.unwrap());
        }
    );
}

void GifPopup::loadGifBytes(std::vector<uint8_t> const& bytes) {
    auto gif = std::make_shared<gdx::GifData>(gdx::decodeGif(bytes.data(), bytes.size()));
    if (!gif->ok()) {
        this->setStatus(gif->error.empty() ? "GIF no valido" : gif->error, "", "Necesitas un GIF con 2 o mas frames");
        return;
    }
    if (gif->srcW > 0 && (gif->srcW != gif->w || gif->srcH != gif->h)) {      // el decodificador lo redujo para no pasar del tope de memoria
        char nb[160];
        std::snprintf(nb, sizeof nb, "GIF grande (%dx%d, %d frames): reducido a %dx%d para ahorrar memoria", gif->srcW, gif->srcH, int(gif->frames.size()), gif->w, gif->h);
        Notification::create(nb, NotificationIcon::Info, 4.f)->show();
    }
    m_gif = gif;
    m_gifBytes = std::make_shared<const std::vector<uint8_t>>(bytes);
    m_layered = nullptr;
    m_hasResult = false;
    this->refreshLabels();
    this->recompute();
}

// ----------------------------------------------------------------- calculo --

gdx::Options GifPopup::makeOptions(int perFrame) const {
    gdx::Options o;
    auto* mod = Mod::get();
    o.budget = std::max(1, perFrame);
    o.maxRaster = std::clamp(mod->getSettingValue<int>("max-raster"), 64, 1024);
    o.alphaThreshold = std::clamp(mod->getSettingValue<int>("alpha-threshold"), 1, 255);
    o.quality = qualityFromSetting();
    o.invertInk = mod->getSettingValue<bool>("invert-ink");
    // Sin canales libres: color por HSV sobre el canal base, y los estilos de tinta usan los canales 1010/1011.
    o.colorChannels = false;
    o.mainChannels = false;
    o.lightSpecialChannels = true;
    o.grayscale = m_gray;
    o.maxChannels = 0;
    return o;
}

gdx::GdExportOptions GifPopup::makeExportOptions() const {
    auto* mod = Mod::get();
    gdx::GdExportOptions eo;
    eo.longSide = m_sizeU;
    eo.colorChannel = std::clamp(mod->getSettingValue<int>("color-channel"), 1, 1020);
    eo.bleed = std::clamp(mod->getSettingValue<double>("bleed"), 0.0, 2.0);
    eo.flipRotation = mod->getSettingValue<bool>("flip-rotation");
    eo.blackChannel = std::clamp(mod->getSettingValue<int>("black-channel"), 1, 1020);
    eo.whiteChannel = std::clamp(mod->getSettingValue<int>("white-channel"), 1, 1020);
    return eo;
}

int GifPopup::editorRoom() const {
    auto lel = LevelEditorLayer::get();
    int cur = (lel && lel->m_objects) ? int(lel->m_objects->count()) : 0;
    return std::max(0, gdx::kEditorObjectLimit - gdx::kEditorObjectMargin - cur);
}

// Tope total de objetos: el de los ajustes, pero nunca mas de lo que cabe en el editor (limite de GD 2.2 menos lo que ya hay).
int GifPopup::maxTotal() const {
    int s = std::clamp(Mod::get()->getSettingValue<int>("max-total-objects"), 2000, 150000);
    return std::clamp(std::min(s, editorRoom()), 200, 150000);
}

void GifPopup::startPlacement(std::string const& chain, size_t objectCount, std::string const& doneMsg) {
    auto lel = LevelEditorLayer::get();
    if (!lel) return;
    auto runner = PlaceRunner::create();
    if (!runner) return;
    lel->addChild(runner);
    runner->begin(splitChain(chain, 1500), objectCount, Mod::get()->getSettingValue<bool>("select-after"), doneMsg);
}

// Metodo 18: 'Objetos por frame' x frames es el total que se reparte entre las capas.
gdx::LayeredOptions GifPopup::makeLayeredOptions() const {
    auto* mod = Mod::get();
    gdx::LayeredOptions lo;
    lo.perFrame = m_perFrame;
    lo.tree = treeMode();
    lo.method = lsaKind();                                          // 0 = metodos 18/19; 1..10 = metodos 20..29
    lo.maxTotal = maxTotal();
    lo.tol = std::clamp(int(mod->getSettingValue<int>("layered-tolerance")), 8, 80);
    lo.denoise = mod->getSettingValue<bool>("layered-denoise");
    lo.painter = mod->getSettingValue<bool>("layered-rotated") ? 1 : 0;
    lo.channels = m_chan ? paletteSize() : 0;                       // sin el check: HSV
    lo.firstChannel = gdx::kFirstColorChannel;                      // siempre desde el 2: el canal 1 nunca se usa
    lo.reuse = float(std::clamp(mod->getSettingValue<double>("layered-reuse"), 0.0, 10.0) / 100.0);
    lo.effort = effortFromSetting();                                // duracion de calculo (18-29)
    lo.classic = mod->getSettingValue<std::string>("layered-mode") != "Mejorado";
    { auto f = mod->getSettingValue<std::string>("layered-flat");                      // colores planos (estilizado cel)
      lo.posterize = f == "Desactivado" ? 0 : f == "Pocos (14)" ? 14 : f == "Medios (24)" ? 24 : f == "Muchos (40)" ? 40 : -1; }   // "Clasico 0.5.5"
    return lo;
}

// Todo lo que cambia el resultado de las capas: si coincide, el boton Colocar reutiliza el calculo de la vista previa.
std::string GifPopup::layeredKey() const {
    auto* mod = Mod::get();
    char buf[200];
    std::snprintf(buf, sizeof buf, "%d|%d|%d|%d|%d|%d|%d|%d|%d|%.3f|%.3f",
                  m_perFrame, m_maxFrames, int(m_gray), maxTotal(),
                  int(mod->getSettingValue<int>("max-raster")), int(mod->getSettingValue<int>("alpha-threshold")),
                  int(mod->getSettingValue<int>("layered-tolerance")), int(mod->getSettingValue<bool>("layered-denoise")),
                  m_gif ? int(m_gif->frames.size()) : 0,
                  mod->getSettingValue<double>("speed"), mod->getSettingValue<double>("min-delay"));
    std::string key = buf;
    key += mod->getSettingValue<bool>("layered-rotated") ? "|r1|" : "|r0|";
    key += std::to_string(mod->getSettingValue<double>("layered-reuse"));
    key += "|" + mod->getSettingValue<std::string>("quality");
    key += "|E" + std::to_string(effortFromSetting());
    key += "|M" + mod->getSettingValue<std::string>("layered-mode") + "|F" + mod->getSettingValue<std::string>("layered-flat");
    key += treeMode() ? "|T" : "|L";
    key += "|M" + std::to_string(lsaKind());
    key += m_chan ? "|c" : "|h";
    key += std::to_string(paletteSize());
    return key;
}

gdx::GifPlan GifPopup::makePlan() const {
    auto* mod = Mod::get();
    double speed = std::clamp(mod->getSettingValue<double>("speed"), 0.1, 10.0);
    double minDelay = std::clamp(mod->getSettingValue<double>("min-delay"), 0.01, 0.5);
    return gdx::planFrames(*m_gif, m_maxFrames, speed, minDelay);
}

// Objetos por frame realmente usados: el pedido, recortado para no pasar del tope total.
int GifPopup::effectivePerFrame(int frames) const {
    return gdx::gifPerFrameBudget(m_perFrame, frames, maxTotal(), m_chan ? paletteSize() : 0);   // + 1 Color trigger por canal
}

void GifPopup::scheduleRecompute() {
    if (!m_gif || m_build) return;
    this->unschedule(schedule_selector(GifPopup::recompute));
    this->scheduleOnce(schedule_selector(GifPopup::recompute), 0.35f);
}

void GifPopup::recompute(float) {
    if (!m_gif || m_build) return;
    if (m_job) m_job->prog.cancel.store(true);

    gdx::GifPlan plan = makePlan();
    auto job = std::make_shared<PreviewJob>();
    job->id = ++m_jobCounter;
    job->gif = m_gif;
    job->frameIdx = plan.idx.empty() ? 0 : plan.idx[0];
    job->method = method();
    job->opt = makeOptions(effectivePerFrame(plan.count()));
    m_job = job;
    this->setStatus("Calculando vista previa...");

    if (layered()) {
        // Metodo 18: se calculan las capas completas (el resultado se reutiliza al colocar) y la vista previa es el frame 0 montado.
        job->plan = plan;
        job->opt = makeOptions(m_perFrame);
        auto lo = makeLayeredOptions();
        int threads = int(std::clamp<unsigned>(unsigned(std::max(1, Mod::get()->getSettingValue<int>("threads"))), 1u,
                                               std::max(1u, std::thread::hardware_concurrency())));
        std::thread([job, lo, threads]() {
            try {
                job->layered = std::make_shared<gdx::LayeredAnim>(gdx::buildAnim(*job->gif, job->plan, job->opt, lo, &job->prog, threads));
                if (job->layered->ok()) job->result = gdx::animFrameResult(*job->layered, 0);
                else job->result.note = job->layered->note.empty() ? "No se pudieron calcular las capas" : job->layered->note;
            } catch (...) {
                job->result = gdx::Result{};
                job->result.note = "Error interno al calcular";
            }
            job->done.store(true, std::memory_order_release);
        }).detach();
        return;
    }

    std::thread([job]() {
        try {
            job->result = gdx::convert(job->gif->frames[size_t(job->frameIdx)], job->method, job->opt, &job->prog);
        } catch (...) {
            job->result = gdx::Result{};
            job->result.note = "Error interno al calcular";
        }
        job->done.store(true, std::memory_order_release);
    }).detach();
}

void GifPopup::tick(float) {
    this->refreshDumpButton();
    // --- conversion completa en curso
    if (m_build) {
        if (m_build->done.load(std::memory_order_acquire)) {
            auto job = m_build;
            m_build = nullptr;
            if (!job->prog.cancel.load()) this->finishBuild(job);
            return;
        }
        char buf[64];
        std::snprintf(buf, sizeof buf, "Convirtiendo frames... %d%%", int(m_build->prog.fraction.load() * 100.f));
        this->setStatus(buf, "No cierres la ventana");
        return;
    }
    // --- vista previa
    if (!m_job) return;
    if (m_job->done.load(std::memory_order_acquire)) {
        auto job = m_job;
        m_job = nullptr;
        if (job->id == m_jobCounter) this->applyPreview(job);
    }
}

void GifPopup::applyPreview(std::shared_ptr<PreviewJob> job) {
    if (job) {
        m_result = std::move(job->result);
        if (job->layered) {                                   // metodo 18
            m_layered = job->layered->ok() ? job->layered : nullptr;
            m_layeredPlan = job->plan;
            m_layeredKey = layeredKey();
        } else {
            m_layered = nullptr;
        }
        m_hasResult = !m_result.shapes.empty();
        if (!m_hasResult) {
            this->setStatus("No se genero ningun objeto", "", m_result.note);
            return;
        }
        this->buildPreview();
    }
    if (!m_hasResult || !m_gif) return;

    if (layered()) {
        if (!m_layered) return;
        auto const& A = *m_layered;
        size_t objs = A.objectCount() + size_t(gdx::animTriggerCount(A));
        char a2[96], b2[96];
        std::snprintf(a2, sizeof a2, "%d frames | %zu objetos", A.frames, objs);
        if (A.lsa)
            std::snprintf(b2, sizeof b2, "%d capas | %.1f dB | %.1f s", int(A.lsa->layers.size()), A.psnrAvg, m_layeredPlan.total);
        else if (!A.palette.empty())
            std::snprintf(b2, sizeof b2, "%s | %.1f dB | canales %d-%d", A.tree ? "arbol" : "rests", A.psnrAvg, A.firstChannel, A.firstChannel + int(A.palette.size()) - 1);
        else if (A.tree)
            std::snprintf(b2, sizeof b2, "arbol prof. %d | %.1f dB | %.1f s", A.depthMax, A.psnrAvg, m_layeredPlan.total);
        else
            std::snprintf(b2, sizeof b2, "%d rests | %.1f dB | %.1f s", int(A.rests.size()), A.psnrAvg, m_layeredPlan.total);
        this->setStatus(a2, b2, m_result.note);
        return;
    }

    // estimacion del resultado completo
    gdx::GifPlan plan = makePlan();
    int n = plan.count();
    int pf = effectivePerFrame(n);
    size_t kb0 = gdx::toGdString(m_result, makeExportOptions()).size() / 1024;
    size_t objs0 = m_result.shapes.size();
    size_t totalObjs = objs0 * size_t(n) + size_t(gdx::gifTriggerCount(n)) + (m_chan ? size_t(paletteSize()) : size_t(0));
    char a1[96], b1[96];
    std::snprintf(a1, sizeof a1, "%d frames | ~%zu objetos", n, totalObjs);
    std::snprintf(b1, sizeof b1, "~%zu KB | %.1f s de bucle", kb0 * size_t(n) + size_t(n) * 2, plan.total);
    std::string note = m_result.note;
    if (pf < m_perFrame) note = "Limitado a " + std::to_string(pf) + " obj/frame por el tope total. " + note;
    this->setStatus(a1, b1, note);
}

void GifPopup::buildPreview() {
    if (m_preview) { m_preview->removeFromParent(); m_preview = nullptr; }
    int w = m_result.rasterW, h = m_result.rasterH;
    if (w <= 0 || h <= 0) return;
    int k = m_result.mono ? std::clamp(300 / std::max(w, h), 1, 3) : 1;
    gdx::Image img = gdx::renderShapesScaled(m_result.shapes, w, h, k);
    w = img.w; h = img.h;
    std::vector<uint8_t> buf(size_t(w) * h * 4);
    for (size_t i = 0; i < size_t(w) * h; ++i) {                    // composita sobre gris oscuro
        const uint8_t* p = &img.rgba[i * 4];
        float a = p[3] / 255.f;
        for (int c = 0; c < 3; ++c) buf[i * 4 + c] = uint8_t(p[c] * a + 40 * (1.f - a));
        buf[i * 4 + 3] = 255;
    }
    auto tex = new CCTexture2D();
    if (!tex->initWithData(buf.data(), kCCTexture2DPixelFormat_RGBA8888, w, h, CCSize(float(w), float(h)))) {
        delete tex;
        return;
    }
    tex->autorelease();
    m_preview = CCSprite::createWithTexture(tex);
    if (!m_preview) return;
    float aspect = m_result.aspect > 0 ? m_result.aspect : 1.f;
    float tw = aspect >= 1.f ? kPrevSize : kPrevSize * aspect;
    float th = aspect >= 1.f ? kPrevSize / aspect : kPrevSize;
    m_preview->setScaleX(tw / w);
    m_preview->setScaleY(th / h);
    m_preview->setPosition({kPrevSize / 2, kPrevSize / 2});
    m_previewHolder->addChild(m_preview);
}

// --------------------------------------------------------------- colocar ----

CCPoint GifPopup::placementOrigin() const {
    // Con un unico objeto seleccionado, el GIF se centra en el; si no, en el centro de la vista del editor.
    if (m_editor) {
        auto sel = m_editor->getSelectedObjects();
        if (sel && sel->count() == 1) {
            auto obj = static_cast<GameObject*>(sel->objectAtIndex(0));
            return {obj->getPositionX(), obj->getPositionY()};
        }
    }
    auto lel = LevelEditorLayer::get();
    auto win = CCDirector::get()->getWinSize();
    if (lel && lel->m_objectLayer) {
        return lel->m_objectLayer->convertToNodeSpace(ccp(win.width / 2, win.height / 2));
    }
    return {0, 0};
}

// Grupo mas alto que ya usa el nivel (objetos y destinos de triggers). Escrito con "requires" para
// compilar aunque los bindings cambien la forma de m_groups.
int GifPopup::maxUsedGroup() const {
    int mx = 0;
    auto lel = LevelEditorLayer::get();
    if (!lel || !lel->m_objects) return 0;
    auto scan = [&](auto* o) {
        if constexpr (requires { o->m_groups; o->m_groupCount; }) {
            auto* arr = o->m_groups;
            if (arr) {
                int cnt = std::min(int(o->m_groupCount), 10);
                for (int i = 0; i < cnt; ++i) {
                    int v;
                    if constexpr (requires { (*arr)[0]; }) v = int((*arr)[size_t(i)]);
                    else v = int(arr[i]);
                    mx = std::max(mx, v);
                }
            }
        }
    };
    for (auto* obj : CCArrayExt<GameObject*>(lel->m_objects)) {
        if (!obj) continue;
        scan(obj);
        if (auto* eff = typeinfo_cast<EffectGameObject*>(obj)) {
            mx = std::max(mx, int(eff->m_targetGroupID));
            mx = std::max(mx, int(eff->m_centerGroupID));
        }
    }
    return mx;
}

void GifPopup::onPlace(CCObject*) {
    if (m_build) return;
    if (m_job) {
        FLAlertLayer::create("GIF Importer", "Todavia se esta calculando la vista previa. Espera un momento.", "OK")->show();
        return;
    }
    if (!m_gif || !m_hasResult) {
        FLAlertLayer::create("GIF Importer", "Primero elige un GIF animado.", "OK")->show();
        return;
    }
    if (!LevelEditorLayer::get()) return;
    if (this->editorRoom() < 300) {
        FLAlertLayer::create("GIF Importer", "El nivel ya esta casi en el limite de objetos del editor (200000). No hay sitio para el GIF.", "OK")->show();
        return;
    }

    auto job = std::make_shared<BuildJob>();
    job->gif = m_gif;
    job->plan = makePlan();
    job->method = method();
    job->opt = makeOptions(effectivePerFrame(job->plan.count()));

    if (layered()) {                                            // metodo 18
        if (m_layered && m_layered->ok() && m_layeredKey == layeredKey()) {
            job->layered = m_layered;                           // ya calculado por la vista previa: se coloca directo
            job->plan = m_layeredPlan;
            job->reused = true;
            job->done.store(true, std::memory_order_release);
            m_build = job;
            return;
        }
        job->opt = makeOptions(m_perFrame);
        auto lo = makeLayeredOptions();
        unsigned hwl = std::max(1u, std::thread::hardware_concurrency());
        job->threads = int(std::clamp<unsigned>(unsigned(std::max(1, Mod::get()->getSettingValue<int>("threads"))), 1u, hwl));
        m_build = job;
        this->unschedule(schedule_selector(GifPopup::recompute));
        this->setStatus("Calculando capas... 0%", "No cierres la ventana");
        std::thread([job, lo]() {
            try {
                job->layered = std::make_shared<gdx::LayeredAnim>(gdx::buildAnim(*job->gif, job->plan, job->opt, lo, &job->prog, job->threads));
            } catch (...) {
                job->layered = nullptr;
            }
            job->done.store(true, std::memory_order_release);
        }).detach();
        return;
    }

    unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    job->threads = int(std::clamp<unsigned>(unsigned(std::max(1, Mod::get()->getSettingValue<int>("threads"))), 1u, hw));
    m_build = job;
    this->unschedule(schedule_selector(GifPopup::recompute));
    this->setStatus("Convirtiendo frames... 0%", "No cierres la ventana");

    job->useCh = m_chan;
    job->paletteK = paletteSize();
    std::thread([job]() {
        try {
            job->results = gdx::convertFrames(*job->gif, job->plan, job->method, job->opt, &job->prog, job->threads);
            if (job->useCh && !job->results.empty()) job->palette = gdx::quantizeFrames(job->results, job->paletteK);   // paleta comun, canales desde el 2
        } catch (...) {
            job->results.clear();
        }
        job->done.store(true, std::memory_order_release);
    }).detach();
}


// Ajuste "save-report": guarda en <carpeta del mod>/diagnostics/<fecha>_m<metodo>/ el informe, la cadena de GD, las tablas de objetos y triggers y las imagenes
// de comparacion (ver engine/diag.hpp). Nunca debe romper la colocacion: cualquier fallo se ignora.
void GifPopup::saveDiagnostics(std::shared_ptr<BuildJob> const& job, gdx::GifExportParams const& p, std::string const& chain, std::string const& note) {
    auto* mod = Mod::get();
    if (!mod->getSettingValue<bool>("save-report") || !job || !job->gif || chain.empty()) return;
    try {
        std::vector<gdx::Result> frames;
        if (job->layered && job->layered->ok()) {
            for (int f = 0; f < job->layered->frames; ++f) frames.push_back(gdx::animFrameResult(*job->layered, f));
        } else {
            frames = job->results;
        }
        const std::string settingsStr = this->settingsText();

        const std::time_t now = std::time(nullptr); std::tm lt{};
#ifdef _WIN32
        localtime_s(&lt, &now);
#else
        localtime_r(&now, &lt);
#endif
        char stamp[32]; std::strftime(stamp, sizeof stamp, "%Y%m%d_%H%M%S", &lt);
        char when[48]; std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", &lt);

        gdx::diag::Input in;
        in.chain = &chain; in.gif = job->gif.get(); in.plan = &job->plan; in.frames = &frames; in.layered = job->layered.get();
        in.settingsText = settingsStr; in.methodName = m_methodLabel ? std::string(m_methodLabel->getString()) : std::string("?");
        in.modVersion = kModVersion; in.when = when;
        in.platformText = std::string("plataforma: ")
#ifdef GEODE_PLATFORM_NAME
            + GEODE_PLATFORM_NAME
#else
            + "?"
#endif
#ifdef GEODE_GD_VERSION_STRING
            + "   GD " + GEODE_GD_VERSION_STRING
#endif
            ;
        in.note = note;
        in.calcSeconds = job->reused ? -1.0 : std::chrono::duration<double>(std::chrono::steady_clock::now() - job->t0).count();
        in.editorRoom = this->editorRoom(); in.threads = job->threads; in.baseGroup = p.baseGroup;
        in.originX = p.eo.originX; in.originY = p.eo.originY; in.trigX = p.trigX; in.longSide = p.eo.longSide; in.channels = m_chan; in.gray = m_gray;
        gdx::diag::Output out = gdx::diag::build(in);

        namespace fs = std::filesystem;
        const fs::path root = mod->getSaveDir() / "diagnostics";
        const fs::path dir = root / (std::string(stamp) + "_m" + std::to_string(m_method + 1));
        std::error_code ec; fs::create_directories(dir, ec);
        auto put = [&](const char* name, const char* data, size_t n) { std::ofstream f(dir / name, std::ios::binary); if (f) f.write(data, std::streamsize(n)); };
        put("informe.txt", out.informe.data(), out.informe.size());
        put("cadena.txt", chain.data(), chain.size());
        put("objetos.tsv", out.objetos.data(), out.objetos.size());
        put("triggers.tsv", out.triggers.data(), out.triggers.size());
        if (!out.comparacion.empty()) put("comparacion.png", reinterpret_cast<const char*>(out.comparacion.data()), out.comparacion.size());
        if (m_gifBytes && !m_gifBytes->empty() && m_gifBytes->size() <= 40u * 1024u * 1024u) put("original.gif", reinterpret_cast<const char*>(m_gifBytes->data()), m_gifBytes->size());   // el GIF de entrada: permite reproducir todo
        if (!out.detalle.empty()) put("detalle.png", reinterpret_cast<const char*>(out.detalle.data()), out.detalle.size());

        // solo se conservan los 8 informes mas recientes (cada uno puede pesar varios MB)
        std::vector<fs::path> all; for (auto& e : fs::directory_iterator(root, ec)) if (e.is_directory(ec)) all.push_back(e.path());
        std::sort(all.begin(), all.end());
        for (size_t i = 0; i + 8 < all.size(); ++i) fs::remove_all(all[i], ec);

        Notification::create("Informe guardado en: " + dir.string(), NotificationIcon::Success, 6.f)->show();
        log::info("GIF Importer: informe de diagnostico en {}", dir.string());
    } catch (...) {
        Notification::create("No se pudo guardar el informe de diagnostico", NotificationIcon::Error, 4.f)->show();
    }
}

// ===================================================================================================================
//  Diagnostico de la interfaz. Ajustes: "ui-dump" (muestra el boton UI), "ui-dump-auto" (volcado automatico al abrir y cuando
//  cambia el estado) y "ui-dump-scene" (ademas, arbol de toda la escena en un segundo archivo).
//  Se guarda en <carpeta del mod>/ui-dumps/ui_<fecha>_<motivo>.txt: TODO lo de la ventana (clase, id, posicion, ancla, tamano, escala,
//  giro, z, visibilidad, color, opacidad, texto, fuente, estado de botones/checks/sliders, caja en unidades de la ventana), los ajustes,
//  el estado interno y comprobaciones automaticas (textos solapados, cosas fuera de la ventana...). Con el boton UI tambien se copia al portapapeles.
// ===================================================================================================================

std::string GifPopup::settingsText() const {
    auto* mod = Mod::get();
    std::ostringstream st;
    st << "[ajustes del mod]\n";
    for (const char* k : {"max-total-objects", "first-group", "threads", "max-raster", "alpha-threshold", "color-channel", "black-channel", "white-channel", "layered-tolerance", "layered-palette-size"})
        st << k << " = " << mod->getSettingValue<int>(k) << "\n";
    for (const char* k : {"speed", "min-delay", "bleed", "layered-reuse"}) st << k << " = " << mod->getSettingValue<double>(k) << "\n";
    for (const char* k : {"triggers-at-start", "flip-rotation", "select-after", "invert-ink", "layered-rotated", "layered-denoise", "use-toggles", "save-report", "ui-dump", "ui-dump-auto", "ui-dump-scene"})
        st << k << " = " << (mod->getSettingValue<bool>(k) ? "true" : "false") << "\n";
    for (const char* k : {"place-mode", "quality", "compute-effort", "layered-mode", "layered-flat"}) st << k << " = " << mod->getSettingValue<std::string>(k) << "\n";
    st << "[ventana]\nindice de metodo = " << m_method << " (metodo " << (m_method + 1) << ")\nobjetos por frame = " << m_perFrame << "\nlado largo (u) = " << m_sizeU
       << "\nframes maximos = " << m_maxFrames << "\ngris = " << m_gray << "\ncanales = " << m_chan << "\ntamano de paleta = " << paletteSize() << "\n";
    return st.str();
}

std::string GifPopup::uiStateText() const {
    auto S = [](CCLabelBMFont* l) { const char* t = l ? l->getString() : nullptr; return t ? std::string(t) : std::string("(null)"); };
    std::ostringstream o;
    o << "tamano de la ventana (constantes) = " << kPopupW << " x " << kPopupH << "   vista previa = " << kPrevSize << " x " << kPrevSize << "\n";
    o << "metodo (indice) = " << m_method << "   etiqueta = \"" << S(m_methodLabel) << "\"\n";
    o << "descripcion = \"" << S(m_descLabel) << "\"\n";
    o << "estado A = \"" << S(m_statA) << "\"   estado B = \"" << S(m_statB) << "\"   nota = \"" << S(m_noteLabel) << "\"\n";
    o << "valor objetos por frame = \"" << S(m_perFrameValue) << "\"   valor lado largo = \"" << S(m_sizeValue) << "\"   valor frames = \"" << S(m_framesValue) << "\"\n";
    o << "objetos por frame = " << m_perFrame << "   lado largo = " << m_sizeU << "   frames maximos = " << m_maxFrames << "   gris = " << m_gray << "   canales = " << m_chan << "\n";
    o << "slider objetos/frame: " << gdx::uic::sliderExtra(m_perFrameSlider) << "  slider lado: " << gdx::uic::sliderExtra(m_sizeSlider) << "  slider frames: " << gdx::uic::sliderExtra(m_framesSlider) << "\n";
    if (m_gif) o << "GIF cargado: " << m_gif->w << "x" << m_gif->h << " px, " << m_gif->frames.size() << " frames\n"; else o << "GIF cargado: no\n";
    o << "vista previa: resultado = " << (m_hasResult ? "si" : "no") << "   raster " << m_result.rasterW << "x" << m_result.rasterH << "   aspecto " << m_result.aspect
      << "   formas " << m_result.shapes.size() << "   sprite = " << (m_preview ? "si" : "no") << "\n";
    o << "calculo de vista previa en curso = " << (m_job ? "si" : "no") << "   colocacion en curso = " << (m_build ? "si" : "no") << "\n";
    return o.str();
}

void GifPopup::refreshDumpButton() {
    if (!m_dumpBtn) return;
    const bool on = Mod::get()->getSettingValue<bool>("ui-dump");
    if (m_dumpBtn->isVisible() != on) { m_dumpBtn->setVisible(on); m_dumpBtn->setEnabled(on); }
}

void GifPopup::scheduleAutoDump(char const* reason, float delay) {
    auto* mod = Mod::get();
    if (!mod->getSettingValue<bool>("ui-dump") || !mod->getSettingValue<bool>("ui-dump-auto")) return;
    m_dumpReason = reason ? reason : "auto";
    this->unschedule(schedule_selector(GifPopup::autoDump));
    this->scheduleOnce(schedule_selector(GifPopup::autoDump), delay);
}

void GifPopup::autoDump(float) {
    if (m_job || m_build) { this->scheduleOnce(schedule_selector(GifPopup::autoDump), 1.0f); return; }   // espera a que termine el calculo
    this->dumpUi(m_dumpReason.empty() ? std::string("auto") : m_dumpReason, false);
}

void GifPopup::onDumpUi(CCObject*) {
    this->dumpUi("manual", true);
}

void GifPopup::dumpUi(std::string const& reason, bool manual) {
    auto* mod = Mod::get();
    try {
        namespace fs = std::filesystem;
        std::string stamp, when;
        nowStrings(stamp, when);
        std::string safe;
        for (char c : reason) safe += std::isalnum(static_cast<unsigned char>(c)) ? c : '-';
        if (safe.empty()) safe = "ui";

        // ---- la ventana: la caja de cada nodo se expresa en unidades de m_mainLayer (X desde la izquierda, Y desde arriba, como init())
        CCNode* ref = m_mainLayer;
        const CCSize rs = ref ? ref->getContentSize() : CCSize(kPopupW, kPopupH);
        gdx::uic::Limits lim; lim.maxNodes = 3000; lim.maxChildren = 400; lim.maxDepth = 40;
        gdx::uic::Collector col(ref, lim);
        std::vector<gdx::ui::Node> nodes = col.run(this);
        const std::vector<std::string> issues = gdx::ui::analyze(nodes, rs.width, rs.height);

        std::ostringstream R;
        R << "=== VOLCADO DE LA INTERFAZ - GIF Importer " << kModVersion << " ===\n"
          << "fecha: " << when << "\nmotivo: " << reason << (manual ? " (boton UI)" : " (automatico)") << "\n" << platformString() << "\n\n"
          << "[LEEME] Coordenadas 'ventana': X desde la izquierda e Y desde ARRIBA de la ventana, en las mismas unidades que usa GifPopup::init (put(x, y)).\n"
          << "'pos / anc / tam / esc / giro' son las propiedades literales de cada nodo (en el espacio de su padre). 'vis=a/b': a = el nodo, b = el nodo y todos sus padres.\n"
          << "'escPantalla' = escala real acumulada hasta la pantalla (1.000 = sin escalar; si no es 1 la ventana se esta animando o esta escalada).\n\n";
        R << "## ENTORNO\n" << gdx::uic::dirInfo(CCDirector::get());
        if (ref) {
            const CCPoint o0 = ref->convertToWorldSpace(ccp(0, 0)), o1 = ref->convertToWorldSpace(ccp(1, 0));
            R << "ventana (m_mainLayer): " << rs.width << " x " << rs.height << "   esquina inferior izquierda en la pantalla = (" << o0.x << ", " << o0.y << ")   escala en pantalla = "
              << std::hypot(o1.x - o0.x, o1.y - o0.y) << "\n";
        }
        R << "\n## ESTADO DE LA VENTANA\n" << this->uiStateText() << "\n## AJUSTES DEL MOD\n" << this->settingsText() << "\n";
        R << "## COMPROBACIONES AUTOMATICAS (" << issues.size() << ")\n";
        if (issues.empty()) R << "(sin avisos)\n"; else for (auto const& l : issues) R << "- " << l << "\n";
        R << "\n## TEXTOS (todos)\n" << gdx::ui::formatTexts(nodes)
          << "\n## BOTONES / ITEMS DE MENU\n" << gdx::ui::formatButtons(nodes)
          << "\n## ARBOL DE LA VENTANA (" << nodes.size() << " nodos" << (col.truncated() ? ", RECORTADO por los limites" : "") << ")\n" << gdx::ui::formatTree(nodes);
        const std::string popupText = R.str();

        // ---- toda la escena (editor incluido), en un segundo archivo: coordenadas de pantalla (Y hacia arriba)
        std::string sceneText;
        if (mod->getSettingValue<bool>("ui-dump-scene")) {
            if (auto* scn = CCDirector::get()->getRunningScene()) {
                gdx::uic::Limits sl; sl.maxNodes = 3000; sl.maxChildren = 60; sl.maxDepth = 30;
                gdx::uic::Collector scol(nullptr, sl);
                std::vector<gdx::ui::Node> sn = scol.run(scn);
                sceneText = "=== ESCENA COMPLETA - GIF Importer " + std::string(kModVersion) + " (" + when + ") ===\n"
                            "Coordenadas 'mundo' = pantalla de cocos (Y hacia ARRIBA). Se listan como maximo 60 hijos por nodo y 3000 nodos; el interior de las etiquetas (letras) no se lista.\n"
                            "nodos listados: " + std::to_string(sn.size()) + (scol.truncated() ? " (RECORTADO)" : "") + "\n\n" + gdx::ui::formatTree(sn, true);
            }
        }

        const fs::path root = mod->getSaveDir() / "ui-dumps";
        std::error_code ec; fs::create_directories(root, ec);
        const fs::path fMain = root / ("ui_" + stamp + "_" + safe + ".txt"), fScene = root / ("ui_" + stamp + "_" + safe + "_escena.txt");
        auto put = [&](fs::path const& f, std::string const& data) { std::ofstream o(f, std::ios::binary); if (o) o.write(data.data(), std::streamsize(data.size())); };
        put(fMain, popupText);
        if (!sceneText.empty()) put(fScene, sceneText);

        {   // solo se conservan los 24 archivos mas recientes (12 volcados)
            std::vector<fs::path> all;
            for (auto& e : fs::directory_iterator(root, ec)) if (e.is_regular_file(ec) && e.path().filename().string().rfind("ui_", 0) == 0) all.push_back(e.path());
            std::sort(all.begin(), all.end());
            for (size_t i = 0; i + 24 < all.size(); ++i) fs::remove(all[i], ec);
        }

        log::info("GIF Importer: volcado de UI en {} ({} nodos, {} avisos)", fMain.string(), nodes.size(), issues.size());
        if (manual) {
            utils::clipboard::write(popupText);                // para pegarlo directamente en el chat
            Notification::create("Volcado de UI guardado y copiado al portapapeles (" + std::to_string(issues.size()) + " avisos). Carpeta: " + root.string(), NotificationIcon::Success, 6.f)->show();
        }
    } catch (...) {
        if (manual) Notification::create("No se pudo guardar el volcado de la interfaz", NotificationIcon::Error, 4.f)->show();
    }
}

void GifPopup::finishBuild(std::shared_ptr<BuildJob> job) {
    auto lel = LevelEditorLayer::get();
    if (!lel) return;
    auto* mod = Mod::get();

    gdx::GifExportParams p;
    p.eo = makeExportOptions();
    p.useToggle = mod->getSettingValue<bool>("use-toggles");      // Toggle triggers en vez de Alpha triggers
    CCPoint origin = placementOrigin();
    p.eo.originX = origin.x;
    p.eo.originY = origin.y;
    const int N = job->plan.count();
    if (job->layered && N >= 4 && m_perFrame < 900) {                    // aviso honesto: con pocos objetos por frame el dibujo sale basico
        const int total = m_perFrame * N, fewer = std::max(2, total / 1500);
        Notification::create("Con " + std::to_string(m_perFrame) + " objetos por frame el resultado sale basico. Con el mismo total (" + std::to_string(total) + ") se ve mucho mas limpio con ~" + std::to_string(fewer) + " frames de 1500 objetos (baja \"Frames maximos\").", NotificationIcon::Info, 7.f)->show();
    }
    p.baseGroup = std::max(this->maxUsedGroup() + 1, std::clamp(mod->getSettingValue<int>("first-group"), 1, 9000));
    if (mod->getSettingValue<bool>("triggers-at-start")) {
        p.trigX = -30.0;
    } else if (job->layered && job->layered->ok()) {
        p.trigX = origin.x - gdx::gdImageSize(job->layered->still, p.eo).w * 0.5 - 30.0;
    } else if (!job->results.empty()) {
        p.trigX = origin.x - gdx::gdImageSize(job->results[0], p.eo).w * 0.5 - 30.0;
    }

    // Modo de diagnostico: objetos estaticos, SIN triggers (hoja de frames o solo el frame 0).
    {
        const std::string pm = mod->getSettingValue<std::string>("place-mode");
        const bool sheet = pm.rfind("Hoja", 0) == 0, solo = pm.rfind("Solo", 0) == 0, calib = pm.rfind("Patron", 0) == 0;
        if (calib) {                                            // patron de calibracion: no usa el GIF
            auto cr = gdx::buildCalibration(p.eo, p.trigX, p.baseGroup, p.useToggle);   // + prueba de bucle (12 frames sinteticos; con Toggle si el ajuste esta activo)
            this->startPlacement(cr.str, 400, std::string("Patron de calibracion + prueba de bucle colocados") + (p.useToggle ? " (con Toggle triggers)" : "") + " (juega el nivel: 12 colores en orden)");
            this->onClose(nullptr);
            return;
        }
        if (sheet || solo) {
            std::vector<gdx::Result> frames;
            std::vector<std::array<uint8_t, 3>> pal;
            int firstCh = gdx::kFirstColorChannel;
            if (job->layered && job->layered->ok()) {
                for (int f = 0; f < job->layered->frames; ++f) frames.push_back(gdx::animFrameResult(*job->layered, f));
                pal = job->layered->palette; firstCh = job->layered->firstChannel;
            } else {
                frames = job->results;
                if (job->useCh) pal = job->palette;
            }
            std::string e2;
            std::string s2 = gdx::buildFrameSheet(frames, p, pal, firstCh, solo, &e2);
            if (s2.empty()) {
                showAlert("GIF Importer", e2.empty() ? std::string("No se pudo generar la hoja de frames.") : e2);
                this->applyPreview(nullptr);
                return;
            }
            size_t nObjs = 0; for (auto& r : frames) nObjs += r.shapes.size();
            if (int(nObjs) > this->editorRoom()) {
                FLAlertLayer::create("GIF Importer", "No caben tantos objetos en el editor (limite 200000). Baja los objetos por frame o los frames.", "OK")->show();
                this->applyPreview(nullptr);
                return;
            }
            char m2[128];
            std::snprintf(m2, sizeof m2, solo ? "Frame 0 colocado (sin triggers)" : "Hoja de %d frames colocada (sin triggers)", int(frames.size()));
            this->saveDiagnostics(job, p, s2, "modo de diagnostico: hoja de frames / solo frame 0 (sin triggers)");
            this->startPlacement(s2, nObjs, m2);
            this->onClose(nullptr);
            return;
        }
    }

    std::string err;
    const bool isLay = job->layered != nullptr;
    if (isLay && !job->layered->ok() && err.empty()) err = "No se pudieron calcular las capas";
    std::string str = isLay ? (job->layered->ok() ? gdx::buildAnimString(*job->layered, job->plan, p, &err) : std::string())
                            : (job->useCh ? gdx::buildGifStringCh(job->results, job->plan, p, job->palette, &err)
                                          : gdx::buildGifString(job->results, job->plan, p, &err));
    if (str.empty()) {
        showAlert("GIF Importer", err.empty() ? std::string("No se pudo generar el GIF.") : err);
        this->applyPreview(nullptr);
        return;
    }

    size_t shapes = 0;
    for (auto& r : job->results) shapes += r.shapes.size();
    size_t count = shapes + size_t(gdx::gifTriggerCount(N)) + (job->useCh && !isLay ? job->palette.size() : size_t(0));   // + 1 Color trigger por canal
    int groupCount = gdx::gifGroupCount(N);
    if (isLay) {
        count = job->layered->objectCount() + size_t(gdx::animTriggerCount(*job->layered));
        groupCount = gdx::animGroupCount(*job->layered);
    }
    if (long(count) > long(this->editorRoom())) {          // GD cortaria la cadena y se perderian objetos: mejor no colocar nada
        char em[200];
        std::snprintf(em, sizeof em, "El GIF necesita %zu objetos y en el editor solo caben %d mas (limite 200000). Baja los objetos por frame o los frames.", count, this->editorRoom());
        showAlert("GIF Importer", em);
        this->applyPreview(nullptr);
        return;
    }
    char msg[160];
    std::snprintf(msg, sizeof msg, "GIF colocado: %d frames, %zu objetos, grupos %d-%d", N, count, p.baseGroup, p.baseGroup + groupCount - 1);
    this->saveDiagnostics(job, p, str, "");
    this->startPlacement(str, count, std::string(msg) + (p.useToggle ? " | con Toggle triggers" : ""));
    this->onClose(nullptr);
}

void GifPopup::onInfo(CCObject*) {
    // MDPopup: ventana con SCROLL y boton OK siempre visible (el FLAlertLayer de antes crecia con el texto y se salia de la pantalla).
    // Es markdown: parrafos separados por una linea en blanco, listas con "- ", colores con <cy>, <cg>, <cr>.
    MDPopup::create(
        "GIF Importer (beta)",
        "Cada frame del GIF se convierte en objetos con el <cy>metodo</c> elegido y va en su propio <cy>grupo</c>.\n\n"
        "Para animarlo se crean <cy>Alpha triggers</c> (o <cy>Toggle triggers</c> si activas \"Usar Toggle\" en Ajustes: menos lag al jugar) que muestran este frame y ocultan el anterior, "
        "y <cy>Spawn triggers</c> con el retardo real del GIF. Un Spawn final se llama a si mismo y la animacion se repite <cg>para siempre</c>.\n\n"
        "- <cg>Objetos por frame</c>: presupuesto de cada frame (se recorta si pasas del tope total de los ajustes).\n"
        "- <cg>Frames maximos</c>: si el GIF tiene mas, se reparten de forma uniforme y se conserva la duracion.\n"
        "- En el editor los frames se ven superpuestos: <cy>dale a jugar</c> para ver la animacion.\n"
        "- Los triggers de arranque van en x=-30 (inicio del nivel); cambialo en Ajustes.\n\n"
        "<cg>Metodo 19 (arbol de deltas)</c>: cada frame se predice del <cy>frame mas parecido</c> y solo pinta lo que cambia. Es el de mas calidad por objeto.\n\n"
        "<cg>Metodo 18 (capas)</c>: lo quieto se traza <cy>una sola vez</c>, mas unas poses de reposo y, por frame, <cy>solo lo que cambia</c>. "
        "\"Objetos por frame\" x frames es el total que se reparte entre las capas: mas nitidez con los mismos objetos.\n\n"
        "No se puede deshacer: <cr>guarda antes</c>.",
        "OK"
    )->show();
}
