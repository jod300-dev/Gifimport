#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include "ui/GifPopup.hpp"

using namespace geode::prelude;

// Boton en la barra "Edit" del editor que abre el importador de GIFs (mismo enganche que Image Exporter).
class $modify(GifImportEditorUI, EditorUI) {
    void onOpenGifImporter(CCObject*) {
        GifPopup::create(this)->show();
    }

    void createMoveMenu() {
        EditorUI::createMoveMenu();

        CCMenuItemSpriteExtra* btn = this->getSpriteButton(
            "gdGifBtn.png"_spr,
            menu_selector(GifImportEditorUI::onOpenGifImporter),
            nullptr, 0.9f
        );
        btn->setID("gif-import-button"_spr);
        m_editButtonBar->m_buttonArray->addObject(btn);

        int rows = GameManager::sharedState()->getIntGameVariable("0049");
        int cols = GameManager::sharedState()->getIntGameVariable("0050");
        m_editButtonBar->reloadItems(rows, cols);
    }
};
