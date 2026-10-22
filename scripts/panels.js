// Plasma desktop script: macOS-style menu bar (top) and dock (bottom).
// Run with: qdbus6 org.kde.plasmashell /PlasmaShell evaluateScript "$(cat panels.js)"
// Replaces ALL existing panels. @HOME@ is substituted by install.sh.

panels().forEach(p => p.remove());

// ---- Top menu bar -------------------------------------------------------
var top = new Panel;
top.location = "top";
top.height = 24;
top.floating = false;
top.lengthMode = "fill";
top.opacity = "translucent";

var launcher = top.addWidget("org.kde.plasma.kickoff");
launcher.currentConfigGroup = ["General"];
launcher.writeConfig("icon", "@HOME@/.local/share/icons/menubar-tux.svg");

// Bold active-app name ("Finder"), needs com.github.antroids.application-title-bar
var title = top.addWidget("com.github.antroids.application-title-bar");
title.currentConfigGroup = ["Appearance"];
title.writeConfig("widgetElements", "windowTitle");
title.writeConfig("windowTitleSource", "0");        // AppName
title.writeConfig("windowTitleFontBold", "true");
title.writeConfig("windowTitleFontSizeMode", "0");  // FixedSize
title.writeConfig("windowTitleFontSize", "10");
title.writeConfig("windowTitleUndefined", "Desktop");
title.writeConfig("windowTitleMarginsLeft", "6");
title.writeConfig("windowTitleMarginsRight", "6");
title.writeConfig("widgetFillWidth", "false");
title.currentConfigGroup = ["Behavior"];
title.writeConfig("widgetActiveTaskSource", "0");   // ActiveTask

top.addWidget("org.kde.plasma.appmenu");
top.addWidget("org.kde.plasma.panelspacer");
top.addWidget("org.kde.plasma.systemtray");

// Spotlight-style search icon (opens KRunner)
var search = top.addWidget("org.kde.plasma.quicklaunch");
search.currentConfigGroup = ["General"];
search.writeConfig("launcherUrls", "file://@HOME@/.local/share/applications/spotlight-search.desktop");
search.writeConfig("showLauncherNames", "false");
search.writeConfig("enablePopup", "false");

var clock = top.addWidget("org.kde.plasma.digitalclock");
clock.currentConfigGroup = ["Appearance"];
clock.writeConfig("autoFontAndSize", "false");
clock.writeConfig("fontFamily", "SF Pro Text");
clock.writeConfig("fontSize", "10");
clock.writeConfig("fontWeight", "400");
clock.writeConfig("showDate", "true");
clock.writeConfig("dateDisplayFormat", "BesideTime");
clock.writeConfig("dateFormat", "custom");
clock.writeConfig("customDateFormat", "ddd d MMM");
clock.writeConfig("use24hFormat", "2");

// ---- Dock ---------------------------------------------------------------
var dock = new Panel;
dock.location = "bottom";
dock.height = 44;
dock.floating = true;
dock.alignment = "center";
dock.lengthMode = "fit";
dock.opacity = "translucent";
dock.hiding = "windowsgobelow";  // keeps it floating when windows touch it

var tasks = dock.addWidget("org.kde.plasma.icontasks");
tasks.currentConfigGroup = ["General"];
tasks.writeConfig("launchers", "applications:systemsettings.desktop,preferred://filemanager,preferred://browser,applications:Alacritty.desktop,applications:teams-for-linux.desktop,applications:dock-trash.desktop");
tasks.writeConfig("iconSpacing", "0");  // macOS-tight icon spacing
tasks.writeConfig("showOnlyCurrentDesktop", "false");
tasks.writeConfig("indicateAudioStreams", "false");
tasks.writeConfig("maxStripes", "1");
