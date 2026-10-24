# fm (irixfm): build with plain g++ + pkg-config. No cmake needed.
#   make            build ./build/irixfm
#   make run        run from the source tree
#   make test       all tests, off-screen (scroll … browser, single instance, D-Bus); run under flock, see CLAUDE.md
#   make tests      build fm and every test binary without running them (use -j: they build in parallel)
#   make check-ci   CI: tests/ci_test.sh runs the suite with per-test logs and one retry for known-flaky tests
#   make tools      build/shot (screenshot harness) and build/leak (leak test); not part of make test
#   make install    install into ~/.local (PREFIX=... to change), with the D-Bus service file for
#                   org.freedesktop.FileManager1 ("Show in folder" in other apps opens fm)
#   make uninstall
PREFIX   ?= $(HOME)/.local
CXX      ?= g++
BUILD    := build
MOC      := $(shell pkg-config --variable=libexecdir Qt6Core 2>/dev/null || echo /usr/lib/qt6)/moc

PKGS     := Qt6DBus Qt6Network Qt6Widgets Qt6Quick Qt6Qml Qt6QuickControls2 Qt6Concurrent Qt6Test KF6WindowSystem KF6CoreAddons KF6Archive
KF6INC   := /usr/include/KF6
# -Wno-deprecated-declarations: KIO 6.6+ headers warn about their own deprecated CacheControl enum.
CXXFLAGS += -std=c++20 -O2 -fPIC -Wall -Wno-deprecated-declarations $(shell pkg-config --cflags $(PKGS)) \
            -I$(KF6INC)/KIOCore -I$(KF6INC)/KIOGui -I$(KF6INC)/KIOWidgets -I$(KF6INC)/KIO \
            -I$(KF6INC)/KConfigCore -I$(KF6INC)/KConfig -I$(KF6INC)/KService -I$(KF6INC)/KJobWidgets -I$(KF6INC)/Solid -I$(KF6INC)/KSyntaxHighlighting -I$(KF6INC)/KPty
LDLIBS   += $(shell pkg-config --libs $(PKGS)) \
            -lKF6KIOCore -lKF6KIOGui -lKF6KIOWidgets -lKF6ConfigCore -lKF6Service -lKF6Solid -lKF6SyntaxHighlighting -lKF6Pty -lvterm

# The shared sources are compiled once into build/obj/*.o and linked into fm and every test (and
# tool), so `make -j tests` builds in parallel without compiling fm.cpp once per test.
OBJ    := $(BUILD)/obj
HDRS   := src/fm.h src/terminal.h src/kiomodel.h src/filemanager1.h
LIBOBJ := $(OBJ)/fm.o $(OBJ)/terminal.o $(OBJ)/kiomodel.o $(OBJ)/moc_fm.o $(OBJ)/moc_terminal.o $(OBJ)/moc_kiomodel.o
MAINOBJ := $(OBJ)/main.o $(OBJ)/moc_filemanager1.o $(LIBOBJ)

all: $(BUILD)/irixfm

$(BUILD)/moc_fm.cpp: src/fm.h | $(BUILD)
	$(MOC) $< -o $@

$(BUILD)/moc_terminal.cpp: src/terminal.h | $(BUILD)
	$(MOC) $< -o $@

$(BUILD)/moc_kiomodel.cpp: src/kiomodel.h | $(BUILD)
	$(MOC) $< -o $@

$(BUILD)/moc_filemanager1.cpp: src/filemanager1.h | $(BUILD)
	$(MOC) $< -o $@

$(OBJ)/%.o: src/%.cpp $(HDRS) | $(OBJ)
	$(CXX) $(CXXFLAGS) -Isrc -c $< -o $@

$(OBJ)/moc_%.o: $(BUILD)/moc_%.cpp $(HDRS) | $(OBJ)
	$(CXX) $(CXXFLAGS) -Isrc -c $< -o $@

$(BUILD)/irixfm: $(MAINOBJ)
	$(CXX) $(CXXFLAGS) $(MAINOBJ) -o $@ $(LDLIBS)

$(BUILD) $(OBJ):
	mkdir -p $@

$(BUILD)/scroll_test: tests/scroll_test.cpp $(LIBOBJ) src/fm.h src/terminal.h
	$(CXX) $(CXXFLAGS) -Isrc -DIRIXFM_SOURCE_DIR='"$(CURDIR)"' tests/scroll_test.cpp $(LIBOBJ) -o $@ $(LDLIBS)

$(BUILD)/path_test: tests/path_test.cpp $(LIBOBJ) src/fm.h src/terminal.h
	$(CXX) $(CXXFLAGS) -Isrc -DIRIXFM_SOURCE_DIR='"$(CURDIR)"' tests/path_test.cpp $(LIBOBJ) -o $@ $(LDLIBS) $(shell pkg-config --libs Qt6Test)

$(BUILD)/sidebar_test: tests/sidebar_test.cpp $(LIBOBJ) src/fm.h src/terminal.h
	$(CXX) $(CXXFLAGS) -Isrc -DIRIXFM_SOURCE_DIR='"$(CURDIR)"' tests/sidebar_test.cpp $(LIBOBJ) -o $@ $(LDLIBS)

$(BUILD)/finder_test: tests/finder_test.cpp $(LIBOBJ) src/fm.h src/terminal.h
	$(CXX) $(CXXFLAGS) -Isrc -DIRIXFM_SOURCE_DIR='"$(CURDIR)"' tests/finder_test.cpp $(LIBOBJ) -o $@ $(LDLIBS)

$(BUILD)/phase2_test: tests/phase2_test.cpp $(LIBOBJ) src/fm.h src/terminal.h
	$(CXX) $(CXXFLAGS) -Isrc -DIRIXFM_SOURCE_DIR='"$(CURDIR)"' tests/phase2_test.cpp $(LIBOBJ) -o $@ $(LDLIBS)

$(BUILD)/tag_test: tests/tag_test.cpp $(LIBOBJ) src/fm.h src/terminal.h
	$(CXX) $(CXXFLAGS) -Isrc -DIRIXFM_SOURCE_DIR='"$(CURDIR)"' tests/tag_test.cpp $(LIBOBJ) -o $@ $(LDLIBS)

$(BUILD)/tab_test: tests/tab_test.cpp $(LIBOBJ) src/fm.h src/terminal.h
	$(CXX) $(CXXFLAGS) -Isrc -DIRIXFM_SOURCE_DIR='"$(CURDIR)"' tests/tab_test.cpp $(LIBOBJ) -o $@ $(LDLIBS)

$(BUILD)/split_test: tests/split_test.cpp $(LIBOBJ) src/fm.h src/terminal.h
	$(CXX) $(CXXFLAGS) -Isrc -DIRIXFM_SOURCE_DIR='"$(CURDIR)"' tests/split_test.cpp $(LIBOBJ) -o $@ $(LDLIBS)

$(BUILD)/dock_test: tests/dock_test.cpp $(LIBOBJ) src/fm.h src/terminal.h
	$(CXX) $(CXXFLAGS) -Isrc -DIRIXFM_SOURCE_DIR='"$(CURDIR)"' tests/dock_test.cpp $(LIBOBJ) -o $@ $(LDLIBS)

$(BUILD)/preview_test: tests/preview_test.cpp $(LIBOBJ) src/fm.h src/terminal.h
	$(CXX) $(CXXFLAGS) -Isrc -DIRIXFM_SOURCE_DIR='"$(CURDIR)"' tests/preview_test.cpp $(LIBOBJ) -o $@ $(LDLIBS)

$(BUILD)/browser_test: tests/browser_test.cpp $(LIBOBJ) src/fm.h
	$(CXX) $(CXXFLAGS) -Isrc -DIRIXFM_SOURCE_DIR='"$(CURDIR)"' tests/browser_test.cpp $(LIBOBJ) -o $@ $(LDLIBS)

$(BUILD)/network_test: tests/network_test.cpp $(LIBOBJ) src/fm.h src/terminal.h src/kiomodel.h
	$(CXX) $(CXXFLAGS) -Isrc -DIRIXFM_SOURCE_DIR='"$(CURDIR)"' tests/network_test.cpp $(LIBOBJ) -o $@ $(LDLIBS) $(shell pkg-config --libs Qt6Test)

$(BUILD)/terminal_test: tests/terminal_test.cpp $(LIBOBJ) src/fm.h src/terminal.h
	$(CXX) $(CXXFLAGS) -Isrc -DIRIXFM_SOURCE_DIR='"$(CURDIR)"' tests/terminal_test.cpp $(LIBOBJ) -o $@ $(LDLIBS)

$(BUILD)/devices_test: tests/devices_test.cpp $(LIBOBJ) src/fm.h src/terminal.h
	$(CXX) $(CXXFLAGS) -Isrc -DIRIXFM_SOURCE_DIR='"$(CURDIR)"' tests/devices_test.cpp $(LIBOBJ) -o $@ $(LDLIBS)

# Dev tools (tools/*.cpp, usage at the top of each file and in CLAUDE.md); not in TESTS.
$(BUILD)/shot: tools/shot.cpp $(LIBOBJ) src/fm.h src/terminal.h
	$(CXX) $(CXXFLAGS) -Isrc tools/shot.cpp $(LIBOBJ) -o $@ $(LDLIBS)

$(BUILD)/leak: tools/leak.cpp $(LIBOBJ) src/fm.h src/terminal.h
	$(CXX) $(CXXFLAGS) -Isrc tools/leak.cpp $(LIBOBJ) -o $@ $(LDLIBS) $(shell pkg-config --libs Qt6Test)

tools: $(BUILD)/shot $(BUILD)/leak

# Off-screen, with throwaway settings, so it never touches the user's screen or config.
TESTS := $(BUILD)/scroll_test $(BUILD)/path_test $(BUILD)/sidebar_test $(BUILD)/finder_test $(BUILD)/phase2_test $(BUILD)/tag_test $(BUILD)/tab_test $(BUILD)/split_test $(BUILD)/dock_test $(BUILD)/preview_test $(BUILD)/devices_test $(BUILD)/terminal_test $(BUILD)/browser_test $(BUILD)/network_test
test: $(TESTS) $(BUILD)/irixfm
	@rc=0; for t in $(TESTS); do echo "== $$(basename $$t)"; tmp=$$(mktemp -d); \
	  XDG_CONFIG_HOME=$$tmp XDG_CACHE_HOME=$$tmp/cache QT_QPA_PLATFORM=offscreen $$t 2>/dev/null || rc=1; rm -rf $$tmp; done; \
	echo "== single_instance_test"; tmp=$$(mktemp -d); XDG_CONFIG_HOME=$$tmp XDG_CACHE_HOME=$$tmp/cache tests/single_instance_test.sh $(BUILD)/irixfm || rc=1; rm -rf $$tmp; \
	echo "== dbus_test"; tmp=$$(mktemp -d); XDG_CONFIG_HOME=$$tmp XDG_CACHE_HOME=$$tmp/cache tests/dbus_test.sh $(BUILD)/irixfm || rc=1; rm -rf $$tmp; \
	exit $$rc

# Build everything `make test` runs, in parallel with -j (each test links its own copy of the sources).
tests: $(TESTS) $(BUILD)/irixfm

test-list:
	@echo $(TESTS)

check-ci: tests
	tests/ci_test.sh

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
	install -d $(PREFIX)/share/dbus-1/services
	sed 's|^Exec=irixfm|Exec=$(PREFIX)/bin/irixfm|' org.freedesktop.FileManager1.service > $(PREFIX)/share/dbus-1/services/org.freedesktop.FileManager1.service
	-busctl --user call org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus ReloadConfig >/dev/null 2>&1
	-update-desktop-database $(PREFIX)/share/applications 2>/dev/null
	-kbuildsycoca6 >/dev/null 2>&1

uninstall:
	rm -f $(PREFIX)/bin/irixfm $(PREFIX)/share/applications/irixfm.desktop $(PREFIX)/share/dbus-1/services/org.freedesktop.FileManager1.service
	-busctl --user call org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus ReloadConfig >/dev/null 2>&1
	rm -rf $(PREFIX)/share/irixfm

clean:
	rm -rf $(BUILD)

.PHONY: all icons run test tests test-list check-ci tools install uninstall clean
