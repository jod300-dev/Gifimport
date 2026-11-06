#include "GifPopup.hpp"

#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/async.hpp>
#include <thread>

namespace {
    constexpr float kPopupW = 430.f, kPopupH = 290.f;
    constexpr float kPrevSize = 140.f;
    constexpr int kMethodCount = int(gdx::Method::Ascii);          // metodos 1-17 (el ASCII no sirve para animar)
    constexpr int kMethodTotal = kMethodCount + 1;                 // + el metodo 18 (capas)
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
    put(m_previewHolder, 84.f, 112.f);

    m_statA = label("Elige un GIF animado", "chatFont.fnt", 0.85f, 84.f, 194.f);
    m_statB = label("", "chatFont.fnt", 0.75f, 84.f, 207.f);
    m_noteLabel = CCLabelBMFont::create("", "chatFont.fnt", 150.f, kCCTextAlignmentCenter);
    m_noteLabel->setScale(0.6f);
    m_noteLabel->setColor({255, 200, 80});
    put(m_noteLabel, 84.f, 236.f);

    // ============ derecha ============
    auto arrow = [&](bool flip, SEL_MenuHandler cb, float x, float y) {
        auto spr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
        spr->setScale(0.5f);
        spr->setFlipX(flip);
        putBtn(CCMenuItemSpriteExtra::create(spr, this, cb), x, y);
    };
    arrow(false, menu_selector(GifPopup::onPrev), 178.f, 52.f);
    arrow(true,  menu_selector(GifPopup::onNext), 406.f, 52.f);
    m_methodLabel = label("", "bigFont.fnt", 0.5f, 292.f, 52.f);
    m_descLabel = CCLabelBMFont::create("", "chatFont.fnt", 380.f, kCCTextAlignmentCenter);
    m_descLabel->setScale(0.66f);
    put(m_descLabel, 292.f, 80.f);

    label("Objetos por frame", "bigFont.fnt", 0.34f, 168.f, 108.f, 0.f);
    m_perFrameValue = label("", "bigFont.fnt", 0.34f, 418.f, 108.f, 1.f);
    m_perFrameSlider = Slider::create(this, menu_selector(GifPopup::onPerFrameSlider), 0.78f);
    m_perFrameSlider->setValue(pfToSlider(m_perFrame));
    put(m_perFrameSlider, 292.f, 124.f);

    label("Lado largo", "bigFont.fnt", 0.34f, 168.f, 146.f, 0.f);
    m_sizeValue = label("", "bigFont.fnt", 0.34f, 418.f, 146.f, 1.f);
    m_sizeSlider = Slider::create(this, menu_selector(GifPopup::onSizeSlider), 0.78f);
    m_sizeSlider->setValue(sizeToSlider(m_sizeU));
    put(m_sizeSlider, 292.f, 162.f);

    label("Frames maximos", "bigFont.fnt", 0.34f, 168.f, 184.f, 0.f);
    m_framesValue = label("", "bigFont.fnt", 0.34f, 418.f, 184.f, 1.f);
    m_framesSlider = Slider::create(this, menu_selector(GifPopup::onFramesSlider), 0.78f);
    m_framesSlider->setValue(framesToSlider(m_maxFrames));
    put(m_framesSlider, 292.f, 200.f);

    m_chkGray = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(GifPopup::onGray), 0.5f);
    m_chkGray->toggle(m_gray);
    putBtn(m_chkGray, 174.f, 224.f);
    label("Escala de grises", "bigFont.fnt", 0.28f, 186.f, 224.f, 0.f);

    auto pickBtn = CCMenuItemSpriteExtra::create(ButtonSprite::create("GIF", "bigFont.fnt", "GJ_button_01.png", 0.6f), this, menu_selector(GifPopup::onPick));
    putBtn(pickBtn, 226.f, 258.f);
    auto placeBtn = CCMenuItemSpriteExtra::create(ButtonSprite::create("Colocar", "bigFont.fnt", "GJ_button_02.png", 0.6f), this, menu_selector(GifPopup::onPlace));
    putBtn(placeBtn, 332.f, 258.f);
    auto infoBtn = CCMenuItemSpriteExtra::create(CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png"), this, menu_selector(GifPopup::onInfo));
    putBtn(infoBtn, 414.f, 258.f);
    auto setBtn = CCMenuItemSpriteExtra::create(ButtonSprite::create("Ajustes", "bigFont.fnt", "GJ_button_04.png", 0.4f), this, menu_selector(GifPopup::onSettings));
    putBtn(setBtn, 40.f, 270.f);

    this->schedule(schedule_selector(GifPopup::tick), 0.1f);
    this->refreshLabels();
    return true;
}

// ------------------------------------------------------------- etiquetas ---

void GifPopup::refreshLabels() {
    if (layered()) {
        m_methodLabel->setString(gdx::layeredName());
        m_descLabel->setString(gdx::layeredDesc());
    } else {
        auto const& info = gdx::methodInfo(method());
        m_methodLabel->setString(info.name);
        m_descLabel->setString(info.desc);
    }
    m_methodLabel->limitLabelWidth(210.f, 0.5f, 0.2f);

    char buf[64];
    std::snprintf(buf, sizeof buf, "%d", m_perFrame);
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
}

void GifPopup::setStatus(std::string const& a, std::string const& b, std::string const& note) {
    m_statA->setString(a.c_str());
    m_statA->limitLabelWidth(150.f, 0.85f, 0.4f);
    m_statB->setString(b.c_str());
    m_statB->limitLabelWidth(150.f, 0.75f, 0.4f);
    m_noteLabel->setString(note.c_str());
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
    if (int64_t(gif->w) * gif->h * int64_t(gif->frames.size()) > 400000000LL) {
        this->setStatus("GIF demasiado grande", "(reduce su tamano o frames)");
        return;
    }
    m_gif = gif;
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

int GifPopup::maxTotal() const {
    return std::clamp(Mod::get()->getSettingValue<int>("max-total-objects"), 2000, 150000);
}

// Metodo 18: 'Objetos por frame' x frames es el total que se reparte entre las capas.
gdx::LayeredOptions GifPopup::makeLayeredOptions() const {
    auto* mod = Mod::get();
    gdx::LayeredOptions lo;
    lo.perFrame = m_perFrame;
    lo.maxTotal = maxTotal();
    lo.tol = std::clamp(int(mod->getSettingValue<int>("layered-tolerance")), 8, 80);
    lo.denoise = mod->getSettingValue<bool>("layered-denoise");
    lo.painter = mod->getSettingValue<bool>("layered-rotated") ? 1 : 0;
    lo.reuse = float(std::clamp(mod->getSettingValue<double>("layered-reuse"), 0.0, 10.0) / 100.0);
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
    return gdx::gifPerFrameBudget(m_perFrame, frames, maxTotal());
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
                job->layered = std::make_shared<gdx::LayeredAnim>(gdx::buildLayered(*job->gif, job->plan, job->opt, lo, &job->prog, threads));
                if (job->layered->ok()) job->result = gdx::layeredFrameResult(*job->layered, 0);
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
        size_t objs = A.objectCount() + size_t(gdx::layeredTriggerCount(A));
        char a2[96], b2[96];
        std::snprintf(a2, sizeof a2, "%d frames | %zu objetos", A.frames, objs);
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
    size_t totalObjs = objs0 * size_t(n) + size_t(gdx::gifTriggerCount(n));
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

    auto job = std::make_shared<BuildJob>();
    job->gif = m_gif;
    job->plan = makePlan();
    job->method = method();
    job->opt = makeOptions(effectivePerFrame(job->plan.count()));

    if (layered()) {                                            // metodo 18
        if (m_layered && m_layered->ok() && m_layeredKey == layeredKey()) {
            job->layered = m_layered;                           // ya calculado por la vista previa: se coloca directo
            job->plan = m_layeredPlan;
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
                job->layered = std::make_shared<gdx::LayeredAnim>(gdx::buildLayered(*job->gif, job->plan, job->opt, lo, &job->prog, job->threads));
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

    std::thread([job]() {
        try {
            job->results = gdx::convertFrames(*job->gif, job->plan, job->method, job->opt, &job->prog, job->threads);
        } catch (...) {
            job->results.clear();
        }
        job->done.store(true, std::memory_order_release);
    }).detach();
}

void GifPopup::finishBuild(std::shared_ptr<BuildJob> job) {
    auto lel = LevelEditorLayer::get();
    if (!lel) return;
    auto* mod = Mod::get();

    gdx::GifExportParams p;
    p.eo = makeExportOptions();
    CCPoint origin = placementOrigin();
    p.eo.originX = origin.x;
    p.eo.originY = origin.y;
    const int N = job->plan.count();
    p.baseGroup = std::max(this->maxUsedGroup() + 1, std::clamp(mod->getSettingValue<int>("first-group"), 1, 9000));
    if (mod->getSettingValue<bool>("triggers-at-start")) {
        p.trigX = -30.0;
    } else if (job->layered && job->layered->ok()) {
        p.trigX = origin.x - gdx::gdImageSize(job->layered->still, p.eo).w * 0.5 - 30.0;
    } else if (!job->results.empty()) {
        p.trigX = origin.x - gdx::gdImageSize(job->results[0], p.eo).w * 0.5 - 30.0;
    }

    std::string err;
    const bool isLay = job->layered != nullptr;
    if (isLay && !job->layered->ok() && err.empty()) err = "No se pudieron calcular las capas";
    std::string str = isLay ? (job->layered->ok() ? gdx::buildLayeredString(*job->layered, job->plan, p, &err) : std::string())
                            : gdx::buildGifString(job->results, job->plan, p, &err);
    if (str.empty()) {
        FLAlertLayer::create("GIF Importer", err.empty() ? "No se pudo generar el GIF." : err.c_str(), "OK")->show();
        this->applyPreview(nullptr);
        return;
    }

    size_t shapes = 0;
    for (auto& r : job->results) shapes += r.shapes.size();
    size_t count = shapes + size_t(gdx::gifTriggerCount(N));
    int groupCount = gdx::gifGroupCount(N);
    if (isLay) {
        count = job->layered->objectCount() + size_t(gdx::layeredTriggerCount(*job->layered));
        groupCount = gdx::layeredGroupCount(*job->layered);
    }
    CCArray* created = lel->createObjectsFromString(str.c_str(), true, true);
    if (created && created->count() > 0 && mod->getSettingValue<bool>("select-after") && m_editor) {
        m_editor->selectObjects(created, true);
    }

    char msg[128];
    std::snprintf(msg, sizeof msg, "GIF colocado: %d frames, %zu objetos, grupos %d-%d",
                  N, count, p.baseGroup, p.baseGroup + groupCount - 1);
    Notification::create(msg, NotificationIcon::Success, 3.5f)->show();
    this->onClose(nullptr);
}

void GifPopup::onInfo(CCObject*) {
    FLAlertLayer::create(
        "GIF Importer (beta)",
        "Cada frame del GIF se convierte en objetos con el <cy>metodo</c> elegido y va en su propio <cy>grupo</c>.\n"
        "Se crean <cy>Alpha triggers</c> (mostrar este frame, ocultar el anterior) y <cy>Spawn triggers</c> con el retardo real del GIF; "
        "un Spawn final se llama a si mismo y la animacion se repite <cg>para siempre</c>.\n"
        "- <cg>Objetos por frame</c>: presupuesto de cada frame (se recorta si pasas del tope total de los ajustes).\n"
        "- <cg>Frames maximos</c>: si el GIF tiene mas, se reparten de forma uniforme y se conserva la duracion.\n"
        "- En el editor los frames se ven superpuestos: <cy>dale a jugar</c> para ver la animacion.\n"
        "- <cg>Metodo 18 (capas)</c>: lo quieto se traza <cy>una sola vez</c>, mas unas poses de reposo y, por frame, <cy>solo lo que cambia</c>. \"Objetos por frame\" x frames es el total que se reparte entre las capas: mas nitidez con los mismos objetos.\n"
        "- Los triggers de arranque van en x=-30 (inicio del nivel); cambialo en Ajustes.\n"
        "No se puede deshacer: <cr>guarda antes</c>.",
        "OK"
    )->show();
}
