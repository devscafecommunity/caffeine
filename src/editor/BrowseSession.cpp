#include "editor/EditorContext.hpp"
#include "editor/AssetBrowser.hpp"
#include "editor/FilePicker.hpp"

namespace Caffeine::Editor {

void serviceBrowseSession(EditorContext& ctx, AssetBrowser& browser) {
    EditorContext::BrowseSession& session = ctx.browse;
    if (session.kind == EditorContext::BrowseSession::Kind::None || session.resultReady) {
        return;
    }

    if (session.kind == EditorContext::BrowseSession::Kind::ProjectAsset) {
        if (!session.started) {
            browser.open();
            browser.beginAssetPicker(session.filter.c_str(), session.title.c_str());
            session.started = true;
        }
        browser.presentPickerModal();

        std::string picked;
        const auto poll = browser.pollAssetPicker(picked);
        if (poll == AssetBrowser::AssetPickerPoll::Selected) {
            if (session.stringTarget) {
                *session.stringTarget = picked;
                ctx.isDirty = true;
                session.clear();
            } else {
                const u32 field = session.fieldId;
                session.pickedText = std::move(picked);
                session.kind = EditorContext::BrowseSession::Kind::None;
                session.started = false;
                session.stringTarget = nullptr;
                session.fieldId = field;
            }
        } else if (poll == AssetBrowser::AssetPickerPoll::Cancelled) {
            session.clear();
        }
        return;
    }

    FilePicker::Mode mode = FilePicker::Mode::PickFile;
    if (session.kind == EditorContext::BrowseSession::Kind::PickFolder) {
        mode = FilePicker::Mode::PickFolder;
    } else if (session.kind == EditorContext::BrowseSession::Kind::SaveFile) {
        mode = FilePicker::Mode::SaveFile;
    }

    if (auto chosen = FilePicker::pickPath(mode, session.title, session.startPath)) {
        if (session.stringTarget) {
            *session.stringTarget = chosen->string();
            ctx.isDirty = true;
            session.clear();
            return;
        }
        session.result = *chosen;
        session.resultReady = true;
        session.kind = EditorContext::BrowseSession::Kind::None;
        return;
    }
    if (FilePicker::consumeCloseEvent(session.title)) {
        session.clear();
    }
}

}  // namespace Caffeine::Editor
