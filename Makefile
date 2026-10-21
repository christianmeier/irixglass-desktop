# fm (irixfm): build with plain g++ + pkg-config. No cmake needed.
#   make            build ./build/irixfm
#   make run        run from the source tree
#   make test       scroll, path bar, sidebar and Finder-feature tests on an off-screen window
#   make install    install into ~/.local (PREFIX=... to change)
#   make uninstall
PREFIX   ?= $(HOME)/.local
CXX      ?= g++
BUILD    := build
MOC      := $(shell pkg-config --variable=libexecdir Qt6Core 2>/dev/null || echo /usr/lib/qt6)/moc

PKGS     := Qt6Widgets Qt6Quick Qt6Qml Qt6QuickControls2 Qt6Concurrent Qt6Test KF6WindowSystem KF6CoreAddons KF6Archive
KF6INC   := /usr/include/KF6
# -Wno-deprecated-declarations: KIO 6.6+ headers warn about their own deprecated CacheControl enum.
CXXFLAGS += -std=c++20 -O2 -fPIC -Wall -Wno-deprecated-declarations $(shell pkg-config --cflags $(PKGS)) \
            -I$(KF6INC)/KIOCore -I$(KF6INC)/KIOGui -I$(KF6INC)/KIOWidgets -I$(KF6INC)/KIO \
            -I$(KF6INC)/KConfigCore -I$(KF6INC)/KConfig -I$(KF6INC)/KService -I$(KF6INC)/KJobWidgets -I$(KF6INC)/Solid -I$(KF6INC)/KSyntaxHighlighting -I$(KF6INC)/KPty
LDLIBS   += $(shell pkg-config --libs $(PKGS)) \
            -lKF6KIOCore -lKF6KIOGui -lKF6KIOWidgets -lKF6ConfigCore -lKF6Service -lKF6Solid -lKF6SyntaxHighlighting -lKF6Pty -lvterm

LIBSRC := src/fm.cpp src/terminal.cpp $(BUILD)/moc_fm.cpp $(BUILD)/moc_terminal.cpp
SRC := src/main.cpp $(LIBSRC)

all: $(BUILD)/irixfm

$(BUILD)/moc_fm.cpp: src/fm.h | $(BUILD)
	$(MOC) $< -o $@

$(BUILD)/moc_terminal.cpp: src/terminal.h | $(BUILD)
	$(MOC) $< -o $@

$(BUILD)/irixfm: $(SRC) src/fm.h src/terminal.h
	$(CXX) $(CXXFLAGS) -Isrc $(SRC) -o $@ $(LDLIBS)

$(BUILD):
	mkdir -p $@

$(BUILD)/scroll_test: tests/scroll_test.cpp $(LIBSRC) src/fm.h src/terminal.h
	$(CXX) $(CXXFLAGS) -Isrc -DIRIXFM_SOURCE_DIR='"$(CURDIR)"' tests/scroll_test.cpp $(LIBSRC) -o $@ $(LDLIBS)

$(BUILD)/path_test: tests/path_test.cpp $(LIBSRC) src/fm.h src/terminal.h
	$(CXX) $(CXXFLAGS) -Isrc -DIRIXFM_SOURCE_DIR='"$(CURDIR)"' tests/path_test.cpp $(LIBSRC) -o $@ $(LDLIBS) $(shell pkg-config --libs Qt6Test)

$(BUILD)/sidebar_test: tests/sidebar_test.cpp $(LIBSRC) src/fm.h src/terminal.h
	$(CXX) $(CXXFLAGS) -Isrc -DIRIXFM_SOURCE_DIR='"$(CURDIR)"' tests/sidebar_test.cpp $(LIBSRC) -o $@ $(LDLIBS)

$(BUILD)/finder_test: tests/finder_test.cpp $(LIBSRC) src/fm.h src/terminal.h
	$(CXX) $(CXXFLAGS) -Isrc -DIRIXFM_SOURCE_DIR='"$(CURDIR)"' tests/finder_test.cpp $(LIBSRC) -o $@ $(LDLIBS)

$(BUILD)/phase2_test: tests/phase2_test.cpp $(LIBSRC) src/fm.h src/terminal.h
	$(CXX) $(CXXFLAGS) -Isrc -DIRIXFM_SOURCE_DIR='"$(CURDIR)"' tests/phase2_test.cpp $(LIBSRC) -o $@ $(LDLIBS)

$(BUILD)/tag_test: tests/tag_test.cpp $(LIBSRC) src/fm.h src/terminal.h
	$(CXX) $(CXXFLAGS) -Isrc -DIRIXFM_SOURCE_DIR='"$(CURDIR)"' tests/tag_test.cpp $(LIBSRC) -o $@ $(LDLIBS)

$(BUILD)/tab_test: tests/tab_test.cpp $(LIBSRC) src/fm.h src/terminal.h
	$(CXX) $(CXXFLAGS) -Isrc -DIRIXFM_SOURCE_DIR='"$(CURDIR)"' tests/tab_test.cpp $(LIBSRC) -o $@ $(LDLIBS)

$(BUILD)/split_test: tests/split_test.cpp $(LIBSRC) src/fm.h src/terminal.h
	$(CXX) $(CXXFLAGS) -Isrc -DIRIXFM_SOURCE_DIR='"$(CURDIR)"' tests/split_test.cpp $(LIBSRC) -o $@ $(LDLIBS)

$(BUILD)/dock_test: tests/dock_test.cpp $(LIBSRC) src/fm.h src/terminal.h
	$(CXX) $(CXXFLAGS) -Isrc -DIRIXFM_SOURCE_DIR='"$(CURDIR)"' tests/dock_test.cpp $(LIBSRC) -o $@ $(LDLIBS)

$(BUILD)/preview_test: tests/preview_test.cpp $(LIBSRC) src/fm.h src/terminal.h
	$(CXX) $(CXXFLAGS) -Isrc -DIRIXFM_SOURCE_DIR='"$(CURDIR)"' tests/preview_test.cpp $(LIBSRC) -o $@ $(LDLIBS)

$(BUILD)/browser_test: tests/browser_test.cpp $(LIBSRC) src/fm.h
	$(CXX) $(CXXFLAGS) -Isrc -DIRIXFM_SOURCE_DIR='"$(CURDIR)"' tests/browser_test.cpp $(LIBSRC) -o $@ $(LDLIBS)

$(BUILD)/terminal_test: tests/terminal_test.cpp $(LIBSRC) src/fm.h src/terminal.h
	$(CXX) $(CXXFLAGS) -Isrc -DIRIXFM_SOURCE_DIR='"$(CURDIR)"' tests/terminal_test.cpp $(LIBSRC) -o $@ $(LDLIBS)

# Off-screen, with throwaway settings, so it never touches the user's screen or config.
TESTS := $(BUILD)/scroll_test $(BUILD)/path_test $(BUILD)/sidebar_test $(BUILD)/finder_test $(BUILD)/phase2_test $(BUILD)/tag_test $(BUILD)/tab_test $(BUILD)/split_test $(BUILD)/dock_test $(BUILD)/preview_test $(BUILD)/terminal_test $(BUILD)/browser_test
test: $(TESTS)
	@rc=0; for t in $(TESTS); do echo "== $$(basename $$t)"; tmp=$$(mktemp -d); \
	  XDG_CONFIG_HOME=$$tmp XDG_CACHE_HOME=$$tmp/cache QT_QPA_PLATFORM=offscreen $$t 2>/dev/null || rc=1; rm -rf $$tmp; done; exit $$rc

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
