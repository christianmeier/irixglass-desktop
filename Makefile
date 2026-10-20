# fm (irixfm): build with plain g++ + pkg-config. No cmake needed.
#   make            build ./build/irixfm
#   make run        run from the source tree
#   make test       scroll test (touchpad + mouse) on an off-screen window
#   make install    install into ~/.local (PREFIX=... to change)
#   make uninstall
PREFIX   ?= $(HOME)/.local
CXX      ?= g++
BUILD    := build
MOC      := $(shell pkg-config --variable=libexecdir Qt6Core 2>/dev/null || echo /usr/lib/qt6)/moc

PKGS     := Qt6Widgets Qt6Quick Qt6Qml Qt6QuickControls2 KF6WindowSystem KF6CoreAddons
KF6INC   := /usr/include/KF6
# -Wno-deprecated-declarations: KIO 6.6+ headers warn about their own deprecated CacheControl enum.
CXXFLAGS += -std=c++20 -O2 -fPIC -Wall -Wno-deprecated-declarations $(shell pkg-config --cflags $(PKGS)) \
            -I$(KF6INC)/KIOCore -I$(KF6INC)/KIOGui -I$(KF6INC)/KIOWidgets -I$(KF6INC)/KIO \
            -I$(KF6INC)/KConfigCore -I$(KF6INC)/KConfig -I$(KF6INC)/KService -I$(KF6INC)/KJobWidgets
LDLIBS   += $(shell pkg-config --libs $(PKGS)) \
            -lKF6KIOCore -lKF6KIOGui -lKF6KIOWidgets -lKF6ConfigCore -lKF6Service

SRC := src/main.cpp src/fm.cpp $(BUILD)/moc_fm.cpp

all: $(BUILD)/irixfm

$(BUILD)/moc_fm.cpp: src/fm.h | $(BUILD)
	$(MOC) $< -o $@

$(BUILD)/irixfm: $(SRC) src/fm.h
	$(CXX) $(CXXFLAGS) -Isrc $(SRC) -o $@ $(LDLIBS)

$(BUILD):
	mkdir -p $@

$(BUILD)/scroll_test: tests/scroll_test.cpp src/fm.cpp $(BUILD)/moc_fm.cpp src/fm.h
	$(CXX) $(CXXFLAGS) -Isrc -DIRIXFM_SOURCE_DIR='"$(CURDIR)"' tests/scroll_test.cpp src/fm.cpp $(BUILD)/moc_fm.cpp -o $@ $(LDLIBS)

# Off-screen, with throwaway settings, so it never touches the user's screen or config.
test: $(BUILD)/scroll_test
	@tmp=$$(mktemp -d) && XDG_CONFIG_HOME=$$tmp QT_QPA_PLATFORM=offscreen $(BUILD)/scroll_test 2>/dev/null; rc=$$?; rm -rf $$tmp; exit $$rc

icons:
	python3 tools/make_icons.py

run: all
	IRIXFM_ASSETS=$(CURDIR) $(BUILD)/irixfm

install: all
	install -Dm755 $(BUILD)/irixfm $(PREFIX)/bin/irixfm
	install -d $(PREFIX)/share/irixfm/qml $(PREFIX)/share/irixfm/icons
	install -m644 qml/*.qml $(PREFIX)/share/irixfm/qml/
	install -m644 icons/*.svg $(PREFIX)/share/irixfm/icons/
	install -m644 README.md $(PREFIX)/share/irixfm/
	sed 's|^Exec=irixfm|Exec=$(PREFIX)/bin/irixfm|' irixfm.desktop > $(PREFIX)/share/applications/irixfm.desktop
	-update-desktop-database $(PREFIX)/share/applications 2>/dev/null
	-kbuildsycoca6 >/dev/null 2>&1

uninstall:
	rm -f $(PREFIX)/bin/irixfm $(PREFIX)/share/applications/irixfm.desktop
	rm -rf $(PREFIX)/share/irixfm

clean:
	rm -rf $(BUILD)

.PHONY: all icons run test install uninstall clean
