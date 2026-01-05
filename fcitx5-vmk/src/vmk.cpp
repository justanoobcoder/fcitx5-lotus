/*
 * SPDX-FileCopyrightText: 2025 CSSlayer <thanhpy2009@gmail.com>
 *
 * SPDX-License-Identifier: GPLv3.0
 *
 */
#include "vmk.h"

#include <fcitx-config/iniparser.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/standardpath.h>
#include <fcitx-utils/textformatflags.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fcitx/menu.h>
#include <fcitx/statusarea.h>
#include <fcitx/userinterface.h>
#include <fcitx/userinterface.h>
#include <fcitx/userinterfacemanager.h>
#include <fcitx-utils/charutils.h>
#include <fcitx-utils/keysymgen.h>
#include <fcitx-utils/utf8.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>

#include <dirent.h>
#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>

#ifdef __linux__
#include <X11/keysym.h>
#include <atomic>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <condition_variable>
#include <mutex>
#include <chrono>
#include <cstdio>
#endif

int E=1;
int EE=1;
int kkk=0;
int Y=0;
std::string BASE_SOCKET_PATH ;
std::string BASE_SOCKET_PATH_TEST;
static const int MAX_SOCKET_ATTEMPTS = 10;
int is_deleting_proxy=0;
static std::string getLastUtf8Char(const std::string &str) ;
static const int MAX_BACKSPACE_COUNT = 15;
static void debugUtf8String(const std::string &str, const char* label = nullptr);
static void debugUtf8String(const char* str, const char* label = nullptr) ;
std::string SubstrChar(const std::string& s, size_t start, size_t len) ;
int compareAndSplitStrings(const std::string& A, const std::string& B, std::string& same, std::string& Adif, std::string& Bdif) ;
static std::atomic<bool> stopListenerThread {false};
std::atomic<bool> stop_flag_monitor{false};
std::atomic<bool> has_reset{false};
std::mutex mtx;
std::condition_variable cv;
extern std::string active_socket_path;
std::atomic<bool> stop_flag{false};
int uinput_fd_ = -1; 
int uinput_client_fd_ = -1;

std::string buildSocketPath(const char* base_path_suffix) {
    const char* username_c = std::getenv("USER");

    if (username_c == nullptr) {
        return "/home/default_user" + std::string(base_path_suffix);
    }

    std::string path = "/home/" + std::string(username_c) + std::string(base_path_suffix);
    return path;
}

void worker() {
    auto start = std::chrono::steady_clock::now();

    std::unique_lock<std::mutex> lock(mtx);
    while (true) {
        if (cv.wait_until(lock, start + std::chrono::seconds(1),
                          [] { return stop_flag.load(); })) {
            if (!has_reset.load()) {
                is_deleting_proxy = 0;
                has_reset.store(true);
            }
            break;
        } else {
            if (!has_reset.load()) {
                is_deleting_proxy = 0;
                has_reset.store(true);
            }
            break;
        }
    }
}

struct KeyEntry {
    uint32_t sym;    
    uint32_t state; 
};

bool isBackspace(uint32_t sym) {
    return sym == 65288 || sym == 8 || sym == FcitxKey_BackSpace;
}

std::string customUtf8Substr(const std::string& str, size_t offset, size_t len = std::string::npos);
size_t customUtf8Length(const std::string& str) ;

namespace fcitx {
namespace {
constexpr std::string_view MacroPrefix = "macro/";
constexpr std::string_view InputMethodActionPrefix = "vmk-input-method-";
constexpr std::string_view CharsetActionPrefix = "vmk-charset-";
const std::string CustomKeymapFile = "conf/vmk-custom-keymap.conf"; 

std::string macroFile(std::string_view imName) {
    return stringutils::concat("conf/vmk-macro-", imName, ".conf");
}

uintptr_t newMacroTable(const vmkMacroTable &macroTable) {
    std::vector<char *> charArray;
    RawConfig r;
    macroTable.save(r);
    for (const auto &keymap : *macroTable.macros) {
        charArray.push_back(const_cast<char *>(keymap.key->data()));
        charArray.push_back(const_cast<char *>(keymap.value->data()));
    }
    charArray.push_back(nullptr);
    return NewMacroTable(charArray.data());
}

std::vector<std::string> convertToStringList(char **array) {
    std::vector<std::string> result;
    for (int i = 0; array[i]; i++) {
        result.push_back(array[i]);
        free(array[i]);
    }
    free(array);
    return result;
}

static void DeletePreviousNChars(fcitx::InputContext *ic,
                                     size_t n,
                                     fcitx::Instance *instance) {

    if (!ic || !instance || n == 0) {
        return;
    }
    if (ic->capabilityFlags().test(fcitx::CapabilityFlag::SurroundingText)) {
        int offset = -static_cast<int>(n);
        ic->deleteSurroundingText(offset, static_cast<int>(n));
        return;
    }
    for (size_t i = 0; i < n; i++) {
        fcitx::Key key(FcitxKey_BackSpace);
        ic->forwardKey(key, false);   
        ic->forwardKey(key, true);    
    }
}

static void DumpICInfo(fcitx::InputContext *ic) {
    if (!ic) {
        return;
    }
}

} 

FCITX_DEFINE_LOG_CATEGORY(vmk, "vmk");
#define FCITX_vmk_DEBUG() FCITX_LOGC(vmk, Debug)

class VMKState final : public InputContextProperty {
public:
    VMKState(vmkEngine *engine, InputContext *ic)
        : engine_(engine), ic_(ic) {
        setEngine();
    }

    ~VMKState() {}

    void setEngine() {
        vmkEngine_.reset(); 
        const std::string currentMode = engine_->config().mode.value();
        if (currentMode == "vmk1") {
            E = 1;
        } else if (currentMode == "vmk2") {
            E = 2;
        } else if (currentMode == "vmkpre") {
            E = 3;
        } else {
            E = 1; 
        }
        EE=1;

        if (engine_->config().inputMethod.value() == "Custom") {
            std::vector<char *> charArray;
            for (const auto &keymap : *engine_->customKeymap().customKeymap) {
                charArray.push_back(const_cast<char *>(keymap.key->data()));
                charArray.push_back(const_cast<char *>(keymap.value->data()));
            }
            charArray.push_back(nullptr);
            vmkEngine_.reset(NewCustomEngine(charArray.data(),
                                                     engine_->dictionary(),
                                                     engine_->macroTable()));
        } else {
            vmkEngine_.reset(NewEngine(engine_->config().inputMethod->data(),
                                                     engine_->dictionary(),
                                                     engine_->macroTable()));
        }
        setOption();
    }

    void setOption() {
      if (!vmkEngine_) {
            return;
        }
       FcitxBambooEngineOption option = {
        .autoNonVnRestore = false, 
        .ddFreeStyle = true, 
        .macroEnabled = false, 
        .autoCapitalizeMacro = false, 
        .spellCheckWithDicts = false, 
        .outputCharset = engine_->config().outputCharset->data(), 
        .modernStyle = false, 
        .freeMarking = true, 
    };

    EngineSetOption(vmkEngine_.handle(), &option);
}

bool connect_uinput_server() {
    if (uinput_client_fd_ >= 0) {
        return true;
    }
    BASE_SOCKET_PATH = buildSocketPath("/.vmksocket/kb_socket");
    const std::string current_path = BASE_SOCKET_PATH;
    int current_fd = -1;

    current_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (current_fd < 0) {
        return false;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, current_path.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(current_fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
        uinput_client_fd_ = current_fd;
        uinput_fd_ = current_fd; 
        return true;
    } 
    
    close(current_fd);
    uinput_client_fd_ = -1;
    uinput_fd_ = -1;
    return false;
}

bool send_command_to_server(const std::string& command) {
    if (uinput_client_fd_ >= 0) {
        if (send(uinput_client_fd_, command.c_str(), command.length(), 0) >= 0) {
            return true; 
        }
    }

    if (uinput_client_fd_ >= 0) {
        close(uinput_client_fd_);
        uinput_client_fd_ = -1;
        uinput_fd_ = -1;
    }

    if (!connect_uinput_server()) {
        return false;
    }

    if (send(uinput_client_fd_, command.c_str(), command.length(), 0) < 0) {
        close(uinput_client_fd_);
        uinput_client_fd_ = -1;
        uinput_fd_ = -1;
        return false;
    }
    
    return true;
}

int setup_uinput() {
    if (connect_uinput_server()) {
        return uinput_fd_; 
    } else {
        return -1;
    }
}

void cleanup_uinput() {
    if (uinput_client_fd_ >= 0) {
        close(uinput_client_fd_);
        uinput_client_fd_ = -1;
        uinput_fd_ = -1; 
    }
}

void send_backspace_uinput(int count) {
    if (uinput_fd_ < 0) { 
        if (!connect_uinput_server()) {
            return;
        }
    }
    
    if (count > MAX_BACKSPACE_COUNT) {
        count = MAX_BACKSPACE_COUNT;
    }

    std::string command = "BACKSPACE_" + std::to_string(count);

    send_command_to_server(command);
}

void start_thread() {
    stop_flag.store(false);
    has_reset.store(false);
    std::thread(worker).detach();
}

void cancel_thread() {
    stop_flag.store(true);
    cv.notify_all(); 
}

bool handleSystemToggle(KeyEvent &event, KeySym currentSym) {
    int modcha=0;

    if (currentSym == FcitxKey_F9) {
        if(E!=1) {
            E=1; modcha=1; reset();
            setup_uinput();
        } else {
            cleanup_uinput(); 
        }
        event.filterAndAccept();
        return true; 
    } 

    if (currentSym == FcitxKey_F8) {
        if(E!=2) {E=2; reset(); modcha=1;}
    }

    if (currentSym == FcitxKey_F11) {
        if(E!=3) {E=3; reset(); modcha=1;}
    }

    if (currentSym == FcitxKey_F12) {
        if(E!=0) {E=0; reset(); modcha=1;}
    }

    if(modcha==1){
        oldPreBuffer_.clear();
        ResetEngine(vmkEngine_.handle()); 
        setOption(); 
        ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
        event.filterAndAccept();
        return true;
    }
    return false;  
}

bool handleUInputKeyPress(fcitx::KeyEvent &event, fcitx::KeySym currentSym) {
    if (!is_deleting_) return false;

    if (isBackspace(currentSym)) {
        current_backspace_count_ += 1;

        if (current_backspace_count_ < expected_backspaces_) {
            return false;
        } else {
            is_deleting_=false; cancel_thread(); is_deleting_proxy=false;      
            usleep(25000); 
            ic_->commitString(pending_commit_string_);

            expected_backspaces_ = 0;
            current_backspace_count_ = -1;
            pending_commit_string_ = "";
            
            event.filterAndAccept(); 
            return true; 
        }
        event.filterAndAccept();  return true; 
    } 
    return false;
}

void keyEvent(KeyEvent &keyEvent) {
    if(Y==1&&(E==2||E==1)) {
        oldPreBuffer_.clear();
        ResetEngine(vmkEngine_.handle()); Y=0;
    }              
    if (!vmkEngine_) {
        return;
    }
    if (keyEvent.isRelease()) {
        return;
    }
    if (keyEvent.rawKey().check(FcitxKey_Shift_L) ||
        keyEvent.rawKey().check(FcitxKey_Shift_R)) {
        return;
    }
    const fcitx::KeySym currentSym = keyEvent.rawKey().sym();
    if (handleSystemToggle(keyEvent, currentSym)) {
        return; 
    }
    
    if(E==1){
        if(is_deleting_==1&&is_deleting_proxy==0) { is_deleting_=0; }

        if (!is_deleting_) {
            bool isResetKey =
                isBackspace(currentSym) ||
                currentSym == FcitxKey_space ||
                currentSym == FcitxKey_Return;

            if (isResetKey) {
                expected_backspaces_ = 0;
                current_backspace_count_ = 0;
                is_deleting_ = false; cancel_thread();
                current_backspace_count_ = -1;
                EngineProcessKeyEvent(
                    vmkEngine_.handle(),
                    keyEvent.rawKey().sym(),
                    keyEvent.rawKey().states());
                UniqueCPtr<char> preeditC(EnginePullPreedit(vmkEngine_.handle()));
                std::string preeditStr = (preeditC && preeditC.get()[0]) ? preeditC.get() : "";
                if( isBackspace(currentSym))
                    oldPreBuffer_ = preeditStr; 
                else {        
                    ResetEngine(vmkEngine_.handle());  
                    oldPreBuffer_.clear();
                }
                keyEvent.forward();
                return; 
            } 
        } else {
            if (isBackspace(currentSym)&&is_deleting_) {
                if (handleUInputKeyPress(keyEvent, currentSym)) {
                    return;  
                }
                return; 
            }
            keyEvent.filterAndAccept();
            return;
        }
      
        {
            auto key = keyEvent.key();
            uint32_t sym = key.sym();

            if (!is_f9_mode_enabled_) {
                is_deleting_ = false;
            }
            if(E==1&&!is_f9_mode_enabled_) {
                is_f9_mode_enabled_=true;
                if (uinput_fd_ < 0) setup_uinput(); 
            }
            if(E==0&&is_f9_mode_enabled_) {is_f9_mode_enabled_=false;}

            if (!is_deleting_&&is_f9_mode_enabled_) {
                if (!EngineProcessKeyEvent(
                        vmkEngine_.handle(),
                        keyEvent.rawKey().sym(),
                        keyEvent.rawKey().states())) {
                    return;
                }
                if (auto commitF = UniqueCPtr<char>(EnginePullCommit(vmkEngine_.handle()));
                    commitF && commitF.get()[0]) {
                    ResetEngine(vmkEngine_.handle());
                    oldPreBuffer_.clear();
                    ic_->inputPanel().reset();
                    ic_->updatePreedit();
                    ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
                    return;
                }     
                
                keyEvent.filterAndAccept();
                if (!oldPreBuffer_.empty()) {
                    size_t len = fcitx::utf8::length(oldPreBuffer_);
                }
                UniqueCPtr<char> preeditC(EnginePullPreedit(vmkEngine_.handle()));
                std::string preeditStr = (preeditC && preeditC.get()[0]) ? preeditC.get() : "";
                if (isBackspace(currentSym)) { 
                    oldPreBuffer_ = preeditStr;        
                    keyEvent.forward();
                    keyEvent.filter(); 
                    return; 
                }
                
                std::string same="";
                std::string Adif=""; 
                std::string Bdif="";
                if (compareAndSplitStrings(oldPreBuffer_,preeditStr, same,Adif,Bdif) ){
                    if(fcitx::utf8::length(Adif)==0) {  
                        ic_->commitString(Bdif);                         
                        oldPreBuffer_ = preeditStr;
                        return; 
                    }
                    size_t len = fcitx::utf8::length(Adif);
                    if (preeditStr != oldPreBuffer_) {
                        if (oldPreBuffer_.empty()) {
                            ic_->commitString(preeditStr);
                            oldPreBuffer_ = preeditStr;
                            keyEvent.accept();
                            return; 
                        }
       
                        if( fcitx::utf8::length(Bdif)>0){
                            const size_t DELETE_COUNT = fcitx::utf8::length(Adif);
                            const std::string COMMIT_STRING = Bdif; 

                            current_backspace_count_ = 0;
                            pending_commit_string_ = Bdif;
                            oldPreBuffer_ = preeditStr;
                            kkk=1;
                            if (uinput_fd_ < 0) {
                                is_f9_mode_enabled_=false;
                                std::string utf8_char = utf8::UCS4ToUTF8(keyEvent.key().sym());
                                if (!utf8_char.empty()) {
                                    keyEvent.accept(); 
                                    keyEvent.inputContext()->commitString(utf8_char);
                                    return ;
                                }
                                return ;
                            }
                            is_deleting_ = true;is_deleting_proxy=true;
                            start_thread();
                            send_backspace_uinput(DELETE_COUNT+1);
                            expected_backspaces_ = DELETE_COUNT+1;
                            return;  
                        }
                    }
                }     
                return;  
            }
            return;
        }
    }
    
    if(E==2) {
        auto ic = keyEvent.inputContext();
        if (!ic) return;

        EngineProcessKeyEvent(vmkEngine_.handle(), keyEvent.rawKey().sym(), keyEvent.rawKey().states());

        if (auto commitF = UniqueCPtr<char>(EnginePullCommit(vmkEngine_.handle()));
            commitF && commitF.get()[0]) {
            ResetEngine(vmkEngine_.handle());
            oldPreBuffer_.clear();
            ic->inputPanel().reset();
            ic->updateUserInterface(UserInterfaceComponent::InputPanel);
            return;
        }

        UniqueCPtr<char> preeditC(EnginePullPreedit(vmkEngine_.handle()));
        std::string preeditStr = (preeditC && preeditC.get()[0]) ? preeditC.get() : "";
 
        if (preeditStr != oldPreBuffer_) {
            keyEvent.filterAndAccept();
            if (!oldPreBuffer_.empty()) {
                size_t len = fcitx::utf8::length(oldPreBuffer_);
                DeletePreviousNChars(ic, len, engine_->instance());
            }
    
            if (!preeditStr.empty()) {
                ic->commitString(preeditStr);
                oldPreBuffer_ = preeditStr;
            } else {
                oldPreBuffer_.clear();
            } 
        }
        return;
    } 

    if (E==3) { 
        if (EngineProcessKeyEvent(vmkEngine_.handle(),
                                      keyEvent.rawKey().sym(),
                                      keyEvent.rawKey().states())) {
            keyEvent.filterAndAccept();
        }
        if (char *commit = EnginePullCommit(vmkEngine_.handle())) {
            if (commit[0]) {
                ic_->commitString(commit);
            }
            free(commit);
        }
        ic_->inputPanel().reset();
        UniqueCPtr<char> preedit(EnginePullPreedit(vmkEngine_.handle()));
        if (preedit && preedit.get()[0]) {
            std::string_view view = preedit.get();
            Text text; 
            TextFormatFlags fmt = TextFormatFlag::NoFlag;
            if (utf8::validate(view)) {
                text.append(std::string(view), fmt);
            }
            text.setCursor(text.textLength());

            if (ic_->capabilityFlags().test(CapabilityFlag::Preedit)) {
                ic_->inputPanel().setClientPreedit(text);
            } else {
                ic_->inputPanel().setPreedit(text);
            }
        }
        ic_->updatePreedit();
        ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
    }
    return;
}

void reset() {
    is_deleting_=0;
    if(E==3){ 
        ic_->inputPanel().reset();
        if (vmkEngine_) {
            ResetEngine(vmkEngine_.handle());
        }
        ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
        ic_->updatePreedit();
    }
    if(E==2){
        oldPreBuffer_.clear();
        ic_->inputPanel().reset();
        if (vmkEngine_) {
            ResetEngine(vmkEngine_.handle());
        }
    }
}

void commitBuffer() {
    if(E==3){
        ic_->inputPanel().reset();
        if (vmkEngine_) {
            EngineCommitPreedit(vmkEngine_.handle());
            UniqueCPtr<char> commit(EnginePullCommit(vmkEngine_.handle()));
            if (commit && commit.get()[0]) {
                ic_->commitString(commit.get());
            }
        }
        ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
        ic_->updatePreedit();
    }
    if(E==2){
        if (vmkEngine_) {
            ResetEngine(vmkEngine_.handle());
        }
    }
}

private:
    vmkEngine *engine_;
    InputContext *ic_;
    CGoObject vmkEngine_;
    std::string oldPreBuffer_;
    bool is_f9_mode_enabled_ = false;  
    std::string raw_key_buffer_; 
    bool is_deleting_ = false;
    size_t expected_backspaces_ = 0;
    size_t current_backspace_count_ = 0;
    std::string pending_commit_string_;
    std::vector<KeyEntry> overlaid_keys_;

    void updateModeAction(fcitx::InputContext *ic);
};

vmkEngine* gBambooEngine = nullptr;
std::atomic<bool> stopMouseThread{false};

static void ResetVMKState(InputContextProperty *state) {}

void mousePressResetThread() {
    BASE_SOCKET_PATH_TEST = buildSocketPath("/.vmksocket/.mouse_flag");
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, BASE_SOCKET_PATH_TEST.c_str(), sizeof(addr.sun_path) - 1);

    while (!stop_flag_monitor.load()) {
        if (uinput_client_fd_ < 0) {
            uinput_client_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
            if (uinput_client_fd_ >= 0) {
                if (connect(uinput_client_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                    close(uinput_client_fd_);
                    uinput_client_fd_ = -1;
                }
            }
        }
        
        if (access(BASE_SOCKET_PATH_TEST.c_str(), F_OK) == 0) {
            Y=1;
            unlink(BASE_SOCKET_PATH_TEST.c_str());
        }
        usleep(500);
    }
    
    if (uinput_client_fd_ >= 0) {
        close(uinput_client_fd_);
    }
    uinput_client_fd_ = -1;
}

void startMouseReset() {
    std::thread(mousePressResetThread).detach();
}

void mousePressResetThread1() {
    Display *display = XOpenDisplay(NULL);
    if (!display) return;

    Window root = DefaultRootWindow(display);
    Window root_ret, child_ret;
    int root_x, root_y, win_x, win_y;
    unsigned int mask_ret;
    bool was_pressed_prev = false; 

    while (!stopMouseThread.load()) { 
        XQueryPointer(display, root, &root_ret, &child_ret,
                      &root_x, &root_y, &win_x, &win_y, &mask_ret);

        bool is_pressed_now = (mask_ret & Button1Mask); 
        if (is_pressed_now && !was_pressed_prev) {
            Y = 1; 
        }
        was_pressed_prev = is_pressed_now;
        usleep(100000); 
    }
    XCloseDisplay(display);
}

void startMouseReset1() {
    std::thread(mousePressResetThread).detach();
}

vmkEngine::vmkEngine(Instance *instance)
    : instance_(instance), factory_([this](InputContext &ic) {
          return new VMKState(this, &ic);
      }) {
    Init();
    {
        auto imNames = convertToStringList(GetInputMethodNames());
        imNames.push_back("Custom");
        imNames_ = std::move(imNames);
    }
    if (std::find(imNames_.begin(), imNames_.end(), "Telex") ==
        imNames_.end()) {
        throw std::runtime_error("Failed to find required input method Telex");
    }
    config_.inputMethod.annotation().setList(imNames_);
    auto fd = StandardPath::global().open(
        StandardPath::Type::PkgData, "bamboo/vietnamese.cm.dict", O_RDONLY);
    if (!fd.isValid()) {
        throw std::runtime_error("Failed to load dictionary");
    }
    dictionary_.reset(NewDictionary(fd.release()));

    auto &uiManager = instance_->userInterfaceManager();

    modeAction_ = std::make_unique<SimpleAction>();
    modeAction_->setIcon("preferences-system");
    modeAction_->setShortText("Mode"); 
    uiManager.registerAction("vmk-mode", modeAction_.get());

    modeMenu_ = std::make_unique<Menu>();
    modeAction_->setMenu(modeMenu_.get());

    std::vector<std::string> modes = {"vmk1", "vmk2", "vmkpre"};

    for (const auto &mId : modes) {
        auto action = std::make_unique<SimpleAction>();
        action->setShortText(mId); 
        action->setCheckable(true);
        
        uiManager.registerAction("vmk-mode-" + mId, action.get());
        
        connections_.emplace_back(action->connect<SimpleAction::Activated>(
            [this, mId](InputContext *ic) {
                if (config_.mode.value() == mId) return;
                config_.mode.setValue(mId);
                saveConfig();

                if (mId == "vmk1") E = 1;
                else if (mId == "vmk2") E = 2;
                else if (mId == "vmkpre") E = 3;

                reloadConfig(); 
                updateModeAction(ic); 
                if (ic) ic->updateUserInterface(fcitx::UserInterfaceComponent::StatusArea);
            }));
        
        modeMenu_->addAction(action.get());
        modeSubAction_.push_back(std::move(action));
    }

    inputMethodAction_ = std::make_unique<SimpleAction>();
    inputMethodAction_->setIcon("document-edit");
    inputMethodAction_->setShortText("Kiểu gõ");
    uiManager.registerAction("vmk-input-method", inputMethodAction_.get());

    inputMethodMenu_ = std::make_unique<Menu>();
    inputMethodAction_->setMenu(inputMethodMenu_.get());

    const std::vector<std::string_view> allowedIMs = {"Telex", "VNI", "Telex W"};
    for (const auto &imName : imNames_) {
        bool isAllowed = false;
        for (const auto &allowedName : allowedIMs) {
            if (imName == allowedName) { isAllowed = true; break; }
        }
        if (!isAllowed) continue;

        inputMethodSubAction_.emplace_back(std::make_unique<SimpleAction>());
        auto action = inputMethodSubAction_.back().get();
        action->setShortText(imName);
        action->setCheckable(true);
        uiManager.registerAction(stringutils::concat(InputMethodActionPrefix, imName), action);

        connections_.emplace_back(action->connect<SimpleAction::Activated>(
            [this, imName](InputContext *ic) {
                if (config_.inputMethod.value() == imName) return;
                config_.inputMethod.setValue(imName);
                saveConfig();
                refreshEngine();
                updateInputMethodAction(ic);
                if (ic) ic->updateUserInterface(fcitx::UserInterfaceComponent::StatusArea);
            }));
        inputMethodMenu_->addAction(action);
    }

    charsetAction_ = std::make_unique<SimpleAction>();
    charsetAction_->setShortText(_("Bảng mã"));
    charsetAction_->setIcon("character-set");
    uiManager.registerAction("vmk-charset", charsetAction_.get());
    charsetMenu_ = std::make_unique<Menu>();
    charsetAction_->setMenu(charsetMenu_.get());

    auto charsets = convertToStringList(GetCharsetNames());
    const std::vector<std::string_view> allowedCharsets = {"Unicode", "TCVN3 (ABC)", "VNI Windows"};
    for (const auto &charset : charsets) {
        bool isAllowed = false;
        for (const auto &allowedName : allowedCharsets) {
            if (charset == allowedName) { isAllowed = true; break; }
        }
        if (!isAllowed) continue;

        charsetSubAction_.emplace_back(std::make_unique<SimpleAction>());
        auto action = charsetSubAction_.back().get();
        action->setShortText(charset);
        action->setCheckable(true);
        uiManager.registerAction(stringutils::concat(CharsetActionPrefix, charset), action);

        connections_.emplace_back(action->connect<SimpleAction::Activated>(
            [this, charset](InputContext *ic) {
                if (config_.outputCharset.value() == charset) return;
                config_.outputCharset.setValue(charset);
                saveConfig();
                refreshEngine();
                updateCharsetAction(ic);
                if (ic) ic->updateUserInterface(fcitx::UserInterfaceComponent::StatusArea);
            }));
        charsetMenu_->addAction(action);
    }
    config_.outputCharset.annotation().setList(charsets);

    reloadConfig();
    instance_->inputContextManager().registerProperty("VMKState", &factory_);
}

void vmkEngine::reloadConfig() {
    readAsIni(config_, "conf/vmk.conf"); 
    readAsIni(customKeymap_, CustomKeymapFile);

    for (const auto &imName : imNames_) {
        auto &table = macroTables_[imName];
        readAsIni(table, macroFile(imName));
        macroTableObject_[imName].reset(newMacroTable(table));
    }
    populateConfig();
}

const Configuration *vmkEngine::getSubConfig(const std::string &path) const {
    if (path == "custom_keymap") {
        return &customKeymap_;
    } else if (stringutils::startsWith(path, MacroPrefix)) {
        const auto imName = path.substr(MacroPrefix.size());
        if (auto iter = macroTables_.find(imName); iter != macroTables_.end()) {
            return &iter->second;
        }
    }
    return nullptr;
}

void vmkEngine::setConfig(const RawConfig &config) {
    config_.load(config, true);
    saveConfig();
    populateConfig();
}

void vmkEngine::populateConfig() {
    refreshEngine();
    refreshOption();
    updateModeAction(nullptr); 
    updateInputMethodAction(nullptr);
    updateCharsetAction(nullptr);
}

void vmkEngine::setSubConfig(const std::string &path, const RawConfig &config) {
    if (path == "custom_keymap") {
        customKeymap_.load(config, true);
        safeSaveAsIni(customKeymap_, CustomKeymapFile);
        refreshEngine();
    } else if (stringutils::startsWith(path, MacroPrefix)) {
        const auto imName = path.substr(MacroPrefix.size());
        if (auto iter = macroTables_.find(imName); iter != macroTables_.end()) {
            iter->second.load(config, true);
            safeSaveAsIni(iter->second, macroFile(imName));
            macroTableObject_[imName].reset(newMacroTable(iter->second));
            refreshEngine();
        }
    }
}

std::string vmkEngine::subMode(const fcitx::InputMethodEntry &, fcitx::InputContext &) {
    return *config_.inputMethod;
}

void vmkEngine::activate(const InputMethodEntry &entry, InputContextEvent &event) {
    FCITX_UNUSED(entry);
    FCITX_UNUSED(event);

    auto ic = event.inputContext(); 

    static std::atomic<bool> mouseThreadStarted{false};
    if (!mouseThreadStarted.exchange(true)) {
        startMouseReset();
    }

    auto &statusArea = event.inputContext()->statusArea();
    if (ic->capabilityFlags().test(fcitx::CapabilityFlag::Preedit)) {
        instance_->inputContextManager().setPreeditEnabledByDefault(true);
    }
    reloadConfig();
    updateModeAction(event.inputContext());
    updateInputMethodAction(event.inputContext());
    updateCharsetAction(event.inputContext());
    statusArea.addAction(StatusGroup::InputMethod, modeAction_.get());
    statusArea.addAction(StatusGroup::InputMethod, inputMethodAction_.get());
    statusArea.addAction(StatusGroup::InputMethod, charsetAction_.get());
}

void vmkEngine::keyEvent(const InputMethodEntry &entry, KeyEvent &keyEvent) {
    FCITX_UNUSED(entry);
    auto state = keyEvent.inputContext()->propertyFor(&factory_);
    state->keyEvent(keyEvent);
}

void vmkEngine::reset(const InputMethodEntry &entry, InputContextEvent &event) {
    FCITX_UNUSED(entry);
    if(E==3){
        auto state = event.inputContext()->propertyFor(&factory_);
        state->reset();
    }
}

void vmkEngine::deactivate(const InputMethodEntry &entry, InputContextEvent &event) {
    FCITX_UNUSED(entry);
    if(E==3){
        auto state = event.inputContext()->propertyFor(&factory_);
        if (event.type() != EventType::InputContextFocusOut) {
            state->commitBuffer();
        } else {
            state->reset();
        }
    }
}

void vmkEngine::refreshEngine() {
    if (!factory_.registered()) return;

    instance_->inputContextManager().foreach([this](InputContext *ic) {
        auto state = ic->propertyFor(&factory_);
        state->setEngine();
        if (ic->hasFocus()) state->reset();
        return true;
    });
}

void vmkEngine::refreshOption() {
    if (!factory_.registered()) return;
    instance_->inputContextManager().foreach([this](InputContext *ic) {
        auto state = ic->propertyFor(&factory_);
        state->setOption();
        if (ic->hasFocus()) state->reset();
        return true;
    });
}

void vmkEngine::updateSpellAction(InputContext *ic) {}
void vmkEngine::updateMacroAction(InputContext *ic) {}

void vmkEngine::updateModeAction(InputContext *ic) {
    std::string currentMode = config_.mode.value();
    for (const auto &action : modeSubAction_) {
        action->setChecked(action->name() == "vmk-mode-" + currentMode);
        if (ic) action->update(ic);
    }
    if (currentMode == "vmk1") E = 1;
    else if (currentMode == "vmk2") E = 2;
    else if (currentMode == "vmkpre") E = 3;
    else E = 1; 

    if (ic) {
        modeAction_->setLongText("Chế độ: " + currentMode);
        modeAction_->update(ic);
    }
}

void vmkEngine::updateInputMethodAction(InputContext *ic) {
    auto name = stringutils::concat(InputMethodActionPrefix, *config_.inputMethod);
    for (const auto &action : inputMethodSubAction_) {
        action->setChecked(action->name() == name); 
        if (ic) action->update(ic);
    }
    if (ic) {
        inputMethodAction_->setLongText(stringutils::concat("Input Method: ", *config_.inputMethod));
        inputMethodAction_->update(ic); 
    }
}

void vmkEngine::updateCharsetAction(InputContext *ic) {
    auto name = stringutils::concat(CharsetActionPrefix, *config_.outputCharset);
    for (const auto &action : charsetSubAction_) {
        action->setChecked(action->name() == name);
        if (ic) action->update(ic);
    }
}

} 

FCITX_ADDON_FACTORY(fcitx::vmkFactory)

static std::string getLastUtf8Char(const std::string &str) {
    if (str.empty()) return "";
    size_t pos = str.size() - 1;
    while (pos > 0 && (static_cast<unsigned char>(str[pos]) & 0xC0) == 0x80) --pos;
    return str.substr(pos);
}

static void debugUtf8String(const std::string &str, const char* label) {}
static void debugUtf8String(const char* str, const char* label ) {}

std::string SubstrChar(const std::string& s, size_t start, size_t len) {
    if (s.empty()) return "";
    const char* start_ptr = fcitx_utf8_get_nth_char(s.c_str(), static_cast<uint32_t>(start));
    if (*start_ptr == '\0') return "";
    if (len == std::string::npos) return std::string(start_ptr);
    const char* end_ptr = fcitx_utf8_get_nth_char(start_ptr, static_cast<uint32_t>(len));
    return std::string(start_ptr, end_ptr - start_ptr);
}

int compareAndSplitStrings(const std::string& A, const std::string& B, std::string& same, std::string& Adif, std::string& Bdif) {
    size_t lengthA = fcitx_utf8_strlen(A.c_str()); 
    size_t lengthB = fcitx_utf8_strlen(B.c_str());
    size_t minLength = std::min(lengthA, lengthB);
    size_t matchLength = 0;
    
    for (size_t i = 0; i < minLength; ++i) {
        const char* ptrA = fcitx_utf8_get_nth_char(A.c_str(), static_cast<uint32_t>(i));
        const char* ptrB = fcitx_utf8_get_nth_char(B.c_str(), static_cast<uint32_t>(i));
        unsigned int lenA = fcitx_utf8_char_len(ptrA);
        unsigned int lenB = fcitx_utf8_char_len(ptrB);
        if (lenA == lenB && std::strncmp(ptrA, ptrB, lenA) == 0) matchLength++;
        else break;
    }
    same = SubstrChar(A, 0, matchLength);
    Adif = SubstrChar(A, matchLength, std::string::npos);
    Bdif = SubstrChar(B, matchLength, std::string::npos);
    return (Adif.empty() && Bdif.empty()) ? 1 : 2;
}

std::string customUtf8Substr(const std::string& str, size_t offset, size_t len ){
    if (offset == 0 && len == std::string::npos) return str;
    size_t byte_start = 0;
    size_t char_count = 0;
    for (size_t i = 0; i < str.length(); ++i) {
        if (char_count == offset) { byte_start = i; break; }
        if ((str[i] & 0xC0) != 0x80) char_count++;
    }
    if (char_count < offset) return "";
    if (len == std::string::npos) return str.substr(byte_start);
    size_t byte_len = 0;
    size_t char_len_count = 0;
    for (size_t i = byte_start; i < str.length(); ++i) {
        if (char_len_count == len) break;
        if ((str[i] & 0xC0) != 0x80) char_len_count++;
        byte_len++;
    }
    return str.substr(byte_start, byte_len);
}

size_t customUtf8Length(const std::string& str) {
    size_t length = 0;
    for (size_t i = 0; i < str.length(); ++i) {
        if ((str[i] & 0xC0) != 0x80) length++;
    }
    return length;
}

