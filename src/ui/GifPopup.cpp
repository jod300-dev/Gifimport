#include "GifPopup.hpp"

#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/async.hpp>
#include <thread>

namespace {
    constexpr float kPopupW = 400.f, kPopupH = 262.f;
    constexpr float kPrevSize = 140.f;
    constexpr int kMinPF = 10, kMaxPF = 5000;                      // objetos por frame
    constexpr int kMinSize = 150, kMaxSize = 6000;

    // sliders 0..1 <-> valor
    float pfToSlider(int v) { return std::clamp(float(std::log(double(v) / kMinPF) / std::log(double(kMaxPF) / kMinPF)), 0.f, 1.f); }
    int sliderToPf(float s) {
        double u = kMinPF * std::pow(double(kMaxPF) / kMinPF, std::clamp(s, 0.f, 1.f));
        int step = u < 100 ? 5 : u < 500 ? 10 : u < 2000 ? 50 : 100;
        return std::clamp(int(std::lround(u / step)) * step, kMinPF, kMaxPF);
    }
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
    m_perFrame = std::clamp(mod->getSavedValue<int>("per-frame", 300), kMinPF, kMaxPF);
    m_sizeU = std::clamp(mod->getSavedValue<int>("size-u", 600), kMinSize, kMaxSize);
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
    put(m_previewHolder, 90.f, 108.f);

    m_statA = label("Elige un GIF animado", "chatFont.fnt", 0.85f, 90.f, 190.f);
    m_statB = label("", "chatFont.fnt", 0.75f, 90.f, 203.f);
    m_noteLabel = CCLabelBMFont::create("", "chatFont.fnt", 160.f, kCCTextAlignmentCenter);
    m_noteLabel->setScale(0.6f);
    m_noteLabel->setColor({255, 200, 80});
    put(m_noteLabel, 90.f, 224.f);

    // ============ derecha ============
    label("Metodo: estatico + parches", "bigFont.fnt", 0.36f, 290.f, 50.f);

    label("Objetos por frame", "bigFont.fnt", 0.34f, 180.f, 82.f, 0.f);
    m_perFrameValue = label("", "bigFont.fnt", 0.34f, 386.f, 82.f, 1.f);
    m_perFrameSlider = Slider::create(this, menu_selector(GifPopup::onPerFrameSlider), 0.78f);
    m_perFrameSlider->setValue(pfToSlider(m_perFrame));
    put(m_perFrameSlider, 290.f, 98.f);

    label("Lado largo", "bigFont.fnt", 0.34f, 180.f, 124.f, 0.f);
    m_sizeValue = label("", "bigFont.fnt", 0.34f, 386.f, 124.f, 1.f);
    m_sizeSlider = Slider::create(this, menu_selector(GifPopup::onSizeSlider), 0.78f);
    m_sizeSlider->setValue(sizeToSlider(m_sizeU));
    put(m_sizeSlider, 290.f, 140.f);

    m_chkGray = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(GifPopup::onGray), 0.55f);
    m_chkGray->toggle(m_gray);
    putBtn(m_chkGray, 192.f, 172.f);
    label("Escala de grises", "bigFont.fnt", 0.3f, 208.f, 172.f, 0.f);

    auto pickBtn = CCMenuItemSpriteExtra::create(ButtonSprite::create("GIF", "bigFont.fnt", "GJ_button_01.png", 0.6f), this, menu_selector(GifPopup::onPick));
    putBtn(pickBtn, 230.f, 212.f);
    auto placeBtn = CCMenuItemSpriteExtra::create(ButtonSprite::create("Colocar", "bigFont.fnt", "GJ_button_02.png", 0.6f), this, menu_selector(GifPopup::onPlace));
    putBtn(placeBtn, 322.f, 212.f);
    auto infoBtn = CCMenuItemSpriteExtra::create(CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png"), this, menu_selector(GifPopup::onInfo));
    putBtn(infoBtn, 380.f, 212.f);
    auto setBtn = CCMenuItemSpriteExtra::create(ButtonSprite::create("Ajustes", "bigFont.fnt", "GJ_button_04.png", 0.5f), this, menu_selector(GifPopup::onSettings));
    putBtn(setBtn, 290.f, 245.f);

    this->schedule(schedule_selector(GifPopup::tick), 0.1f);
    this->refreshLabels();
    return true;
}

// ------------------------------------------------------------- etiquetas ---

void GifPopup::refreshLabels() {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%d", m_perFrame);
    m_perFrameValue->setString(buf);
    std::snprintf(buf, sizeof buf, "%d u (%.1f bloques)", m_sizeU, m_sizeU / 30.0);
    m_sizeValue->setString(buf);

    auto* mod = Mod::get();
    mod->setSavedValue<int>("per-frame", m_perFrame);
    mod->setSavedValue<int>("size-u", m_sizeU);
    mod->setSavedValue<bool>("gray", m_gray);
}

void GifPopup::setStatus(std::string const& a, std::string const& b, std::string const& note) {
    m_statA->setString(a.c_str());
    m_statA->limitLabelWidth(160.f, 0.85f, 0.4f);
    m_statB->setString(b.c_str());
    m_statB->limitLabelWidth(160.f, 0.75f, 0.4f);
    m_noteLabel->setString(note.c_str());
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
    o.grayscale = m_gray;
    return o;
}

gdx::PatchParams GifPopup::makePatchParams(int baseBudgetValue) const {
    auto* mod = Mod::get();
    gdx::PatchParams pp;
    pp.tolerance = std::clamp(mod->getSettingValue<int>("patch-tolerance"), 4, 120);
    pp.baseBudget = baseBudgetValue;
    unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    pp.threads = int(std::clamp<unsigned>(unsigned(std::max(1, mod->getSettingValue<int>("threads"))), 1u, hw));
    return pp;
}

gdx::GdExportOptions GifPopup::makeExportOptions() const {
    auto* mod = Mod::get();
    gdx::GdExportOptions eo;
    eo.longSide = m_sizeU;
    eo.bleed = std::clamp(mod->getSettingValue<double>("bleed"), 0.0, 2.0);
    return eo;
}

int GifPopup::maxTotal() const {
    return std::clamp(Mod::get()->getSettingValue<int>("max-total-objects"), 2000, 150000);
}

// Presupuesto del fondo estatico = limite por frame x multiplicador (ajustes)
int GifPopup::baseBudget() const {
    int mult = std::clamp(Mod::get()->getSettingValue<int>("base-multiplier"), 1, 20);
    return gdx::patchBaseBudget(m_perFrame, mult, maxTotal());
}

// Objetos por parche realmente usados: el pedido, recortado para no pasar del tope total.
int GifPopup::patchBudget(int frames) const {
    return gdx::patchPerFrameBudget(m_perFrame, frames, maxTotal(), baseBudget());
}

gdx::GifPlan GifPopup::makePlan() const {
    auto* mod = Mod::get();
    double speed = std::clamp(mod->getSettingValue<double>("speed"), 0.1, 10.0);
    double minDelay = std::clamp(mod->getSettingValue<double>("min-delay"), 0.01, 0.5);
    int maxFrames = std::clamp(mod->getSettingValue<int>("max-frames"), 2, 300);
    return gdx::planFrames(*m_gif, maxFrames, speed, minDelay);
}

void GifPopup::scheduleRecompute() {
    if (!m_gif || m_build) return;
    this->unschedule(schedule_selector(GifPopup::recompute));
    this->scheduleOnce(schedule_selector(GifPopup::recompute), 0.35f);
}

void GifPopup::recompute(float) {
    if (!m_gif || m_build) return;
    if (m_job) m_job->prog.cancel.store(true);

    auto job = std::make_shared<PreviewJob>();
    job->id = ++m_jobCounter;
    job->gif = m_gif;
    job->plan = makePlan();
    job->opt = makeOptions(patchBudget(job->plan.count()));
    job->pp = makePatchParams(baseBudget());
    job->pp.threads = 1;
    job->pp.maxPatches = 1;                                      // solo fondo + parche 0 (rapido)
    m_job = job;
    this->setStatus("Calculando vista previa...");

    std::thread([job]() {
        try {
            job->patchSet = gdx::convertPatches(*job->gif, job->plan, job->opt, job->pp, &job->prog);
            job->result = gdx::combineForPreview(job->patchSet);
        } catch (...) {
            job->patchSet = gdx::PatchSet{};
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
        m_patchSet = std::move(job->patchSet);
        m_result = std::move(job->result);
        m_hasResult = !m_result.shapes.empty();
        if (!m_hasResult) {
            this->setStatus("No se genero ningun objeto", "", m_result.note);
            return;
        }
        this->buildPreview();
    }
    if (!m_hasResult || !m_gif || m_patchSet.patches.empty()) return;

    // estimacion del resultado completo: fondo (una vez) + n parches parecidos al parche 0
    gdx::GifPlan plan = makePlan();
    int n = plan.count();
    auto eo = makeExportOptions();
    size_t baseObjs = m_patchSet.base.shapes.size(), p0 = m_patchSet.patches[0].shapes.size();
    size_t baseKb = gdx::toGdString(m_patchSet.base, eo).size() / 1024;
    size_t p0Kb = gdx::toGdString(m_patchSet.patches[0], eo).size() / 1024;
    size_t totalObjs = baseObjs + p0 * size_t(n) + size_t(gdx::gifTriggerCount(n));
    char a1[96], b1[96];
    std::snprintf(a1, sizeof a1, "%d frames | ~%zu objetos", n, totalObjs);
    std::snprintf(b1, sizeof b1, "~%zu KB | %.1f s de bucle", baseKb + p0Kb * size_t(n) + size_t(n) * 2, plan.total);
    int pct = m_patchSet.totalPixels ? int(m_patchSet.patchPixels0 * 100 / m_patchSet.totalPixels) : 0;
    std::string note = "Fondo: " + std::to_string(baseObjs) + " obj. Cambia ~" + std::to_string(pct) + "% por frame.";
    if (patchBudget(n) < m_perFrame) note += " Limitado a " + std::to_string(patchBudget(n)) + " obj/frame por el tope total.";
    this->setStatus(a1, b1, note);
}

void GifPopup::buildPreview() {
    if (m_preview) { m_preview->removeFromParent(); m_preview = nullptr; }
    int w = m_result.rasterW, h = m_result.rasterH;
    if (w <= 0 || h <= 0) return;
    gdx::Image img = gdx::renderShapes(m_result.shapes, w, h);
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
    job->opt = makeOptions(patchBudget(job->plan.count()));
    job->pp = makePatchParams(baseBudget());
    m_build = job;
    this->unschedule(schedule_selector(GifPopup::recompute));
    this->setStatus("Convirtiendo frames... 0%", "No cierres la ventana");

    std::thread([job]() {
        try {
            job->patchSet = gdx::convertPatches(*job->gif, job->plan, job->opt, job->pp, &job->prog);
        } catch (...) {
            job->patchSet = gdx::PatchSet{};
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
        p.trigX = double(mod->getSettingValue<int>("trigger-x"));
    } else {
        p.trigX = std::max(0.0, origin.x - gdx::gdImageSize(job->patchSet.base, p.eo).w * 0.5 - 30.0);
    }

    std::string err;
    std::string str = gdx::buildGifPatchString(job->patchSet, job->plan, p, &err);
    if (str.empty()) {
        FLAlertLayer::create("GIF Importer", err.empty() ? "No se pudo generar el GIF." : err.c_str(), "OK")->show();
        this->applyPreview(nullptr);
        return;
    }

    size_t shapes = job->patchSet.base.shapes.size();
    for (auto& r : job->patchSet.patches) shapes += r.shapes.size();
    size_t count = shapes + size_t(gdx::gifTriggerCount(N));
    CCArray* created = lel->createObjectsFromString(str.c_str(), true, true);
    if (created && created->count() > 0 && mod->getSettingValue<bool>("select-after") && m_editor) {
        m_editor->selectObjects(created, true);
    }

    char msg[160];
    std::snprintf(msg, sizeof msg, "GIF colocado: %d frames, %zu objetos, grupos %d-%d, arranque en x=%d",
                  N, count, p.baseGroup, p.baseGroup + gdx::gifGroupCount(N) - 1, int(p.trigX));
    Notification::create(msg, NotificationIcon::Success, 4.f)->show();
    this->onClose(nullptr);
}

void GifPopup::onInfo(CCObject*) {
    FLAlertLayer::create(
        "GIF Importer (beta)",
        "<cg>Metodo: estatico + parches.</c> Lo que no cambia en el GIF se traza <cy>una sola vez</c> (fondo) y cada frame solo lleva "
        "un <cy>parche</c> con lo que cambia.\n"
        "Se crean <cy>Alpha triggers</c> (mostrar este parche, ocultar el anterior) y <cy>Spawn triggers</c> con el retardo real del GIF; "
        "un Spawn final se llama a si mismo y la animacion se repite <cg>para siempre</c>.\n"
        "- <cg>Objetos por frame</c>: limite de cada parche (el fondo usa ese limite x el multiplicador de Ajustes).\n"
        "- En el editor los frames se ven superpuestos: <cy>juega el nivel desde el inicio</c> (sin Start Pos) para ver la animacion.\n"
        "- El trigger de arranque va en x=0 por defecto; cambialo en Ajustes (Trigger de arranque: x).\n"
        "No se puede deshacer: <cr>guarda antes</c>.",
        "OK"
    )->show();
}
