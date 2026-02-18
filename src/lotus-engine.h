/*
 * SPDX-FileCopyrightText: 2022-2022 CSSlayer <wengxt@gmail.com>
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */
#ifndef _FCITX5_LOTUS_ENGINE_H_
#define _FCITX5_LOTUS_ENGINE_H_

#include "lotus-config.h"
#include "emoji.h"
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/i18n.h>
#include <fcitx/action.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace fcitx {

    class CGoObject;
    class LotusState;

    class LotusEngine final : public InputMethodEngine {
      public:
        Instance* instance() const {
            return instance_;
        }

        LotusEngine(Instance* instance);
        ~LotusEngine();

        void                 activate(const InputMethodEntry& entry, InputContextEvent& event) override;
        void                 deactivate(const fcitx::InputMethodEntry& entry, fcitx::InputContextEvent& event) override;
        void                 keyEvent(const InputMethodEntry& entry, KeyEvent& keyEvent) override;

        void                 reset(const InputMethodEntry& entry, InputContextEvent& event) override;

        void                 reloadConfig() override;

        const Configuration* getConfig() const override {
            return &config_;
        }

        const Configuration* getSubConfig(const std::string& path) const override;

        void                 setConfig(const RawConfig& config) override;

        void                 setSubConfig(const std::string& path, const RawConfig& config) override;

        std::string          subMode(const fcitx::InputMethodEntry& entry, fcitx::InputContext& inputContext) override;

        std::string          overrideIcon(const fcitx::InputMethodEntry& entry) override;

        const auto&          config() const {
            return config_;
        }
        const auto& customKeymap() const {
            return customKeymap_;
        }

        uintptr_t dictionary() const {
            return dictionary_.handle();
        }

        uintptr_t macroTable() const;

        void      refreshEngine();
        void      refreshOption();

        void      saveConfig() {
            safeSaveAsIni(config_, "conf/lotus.conf");
        }
        void updateModeAction(InputContext* ic);
        void updateSpellAction(InputContext* ic);
        void updateMacroAction(InputContext* ic);
        void updateCapitalizeMacroAction(InputContext* ic);
        void updateAutoNonVnRestoreAction(InputContext* ic);
        void updateModernStyleAction(InputContext* ic);
        void updateFreeMarkingAction(InputContext* ic);
        void updateFixUinputWithAckAction(InputContext* ic);
        void updateInputMethodAction(InputContext* ic);
        void updateCharsetAction(InputContext* ic);
        void populateConfig();
        // ibus-bamboo mode save/load
        void         loadAppRules();
        void         saveAppRules();
        void         showAppModeMenu(InputContext* ic);
        void         closeAppModeMenu();
        EmojiLoader& emojiLoader() {
            return emojiLoader_;
        }
        void setMode(LotusMode mode, InputContext* ic);

      private:
        Instance*                                         instance_;
        lotusConfig                                       config_;
        lotusCustomKeymap                                 customKeymap_;

        std::unordered_map<std::string, lotusMacroTable>  macroTables_;
        std::unordered_map<std::string, CGoObject>        macroTableObject_;

        FactoryFor<LotusState>                            factory_;
        std::vector<std::string>                          imNames_;

        std::unique_ptr<SimpleAction>                     inputMethodAction_;
        std::vector<std::unique_ptr<SimpleAction>>        inputMethodSubAction_;
        std::unique_ptr<Menu>                             inputMethodMenu_;
        std::unique_ptr<fcitx::SimpleAction>              modeAction_;
        std::unique_ptr<fcitx::Menu>                      modeMenu_;
        std::vector<std::unique_ptr<fcitx::SimpleAction>> modeSubAction_;
        std::unique_ptr<SimpleAction>                     charsetAction_;
        std::vector<std::unique_ptr<SimpleAction>>        charsetSubAction_;
        std::unique_ptr<Menu>                             charsetMenu_;

        std::unique_ptr<SimpleAction>                     spellCheckAction_;
        std::unique_ptr<SimpleAction>                     macroAction_;
        std::unique_ptr<SimpleAction>                     capitalizeMacroAction_;
        std::unique_ptr<SimpleAction>                     autoNonVnRestoreAction_;
        std::unique_ptr<SimpleAction>                     modernStyleAction_;
        std::unique_ptr<SimpleAction>                     freeMarkingAction_;
        std::unique_ptr<SimpleAction>                     fixUinputWithAckAction_;
        std::vector<ScopedConnection>                     connections_;
        CGoObject                                         dictionary_;
        // ibus-bamboo mode save/load
        std::unordered_map<std::string, fcitx::LotusMode> appRules_;
        std::string                                       appRulesPath_;
        bool                                              isSelectingAppMode_ = false;
        std::string                                       currentConfigureApp_;
        LotusMode                                         globalMode_;
        EmojiLoader                                       emojiLoader_;
    };

    class LotusFactory : public AddonFactory {
      public:
        AddonInstance* create(AddonManager* manager) override {
            registerDomain("fcitx5-lotus", FCITX_INSTALL_LOCALEDIR);
            return new LotusEngine(manager->instance());
        }
    };

} // namespace fcitx

#endif // _FCITX5_LOTUS_ENGINE_H_
