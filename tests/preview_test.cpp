// Rich previews: Markdown is rendered (headings, links, pictures resolved), code and fenced code
// blocks are highlighted with the Nord theme when the language is known, other text stays plain.
#include "fm.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <cstdio>

namespace
{
int failures = 0;
void check(bool ok, const QString &what, const QString &got = {})
{
    std::printf("%s %-58s %s\n", ok ? "PASS" : "FAIL", qPrintable(what), ok ? "" : qPrintable("got: " + got.left(160)));
    failures += ok ? 0 : 1;
}
void write(const QString &path, const QByteArray &data)
{
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(data);
}
} // namespace

int main(int argc, char *argv[])
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QApplication app(argc, argv);
    QTemporaryDir tmp;
    const QString base = tmp.path();
    Fm fm(base, QStringLiteral(IRIXFM_SOURCE_DIR));

    check(fm.highlightThemes().contains(QStringLiteral("Nord")), QStringLiteral("the Nord highlighting theme is available"));

    QDir(base).mkdir(QStringLiteral("img"));
    QImage pic(1600, 800, QImage::Format_RGB32);
    pic.fill(Qt::blue);
    pic.save(base + QStringLiteral("/img/shot.png"));
    write(base + QStringLiteral("/notes.md"),
          "# Title\n\nSome *text* and a [link](https://example.org).\n\n![shot](img/shot.png)\n\n"
          "```python\ndef hello(name):\n    return f\"hi {name}\"  # greet\n```\n\n- one\n- two\n");
    const QVariantMap md = fm.richPreview(base + QStringLiteral("/notes.md"));
    const QString h = md.value(QStringLiteral("html")).toString();
    check(md.value(QStringLiteral("kind")) == QStringLiteral("markdown"), QStringLiteral("a .md file is rendered as Markdown"));
    check(h.contains(QStringLiteral("Title")) && !h.contains(QStringLiteral("# Title")), QStringLiteral("…headings are rendered, not shown as #"), h);
    check(h.contains(QStringLiteral("color:#88C0D0;\" href")), QStringLiteral("…links get Nord frost"), h);
    check(h.contains(QStringLiteral("file://") + base + QStringLiteral("/img/shot.png")) && h.contains(QStringLiteral("width=\"640\"")),
          QStringLiteral("…pictures resolve next to the file and are scaled down"), h);
    check(h.contains(QStringLiteral("<span style=\"color:#")) && h.contains(QStringLiteral("hello")) && !h.contains(QStringLiteral("```")),
          QStringLiteral("…a ```python block is syntax-highlighted"), h);

    write(base + QStringLiteral("/tool.py"), "import os\nprint(os.getcwd())\n");
    const QVariantMap py = fm.richPreview(base + QStringLiteral("/tool.py"));
    check(py.value(QStringLiteral("kind")) == QStringLiteral("code") && py.value(QStringLiteral("language")) == QStringLiteral("Python")
              && py.value(QStringLiteral("html")).toString().contains(QStringLiteral("<span style=\"color:#")),
          QStringLiteral("a .py file is highlighted as Python"), py.value(QStringLiteral("language")).toString());
    write(base + QStringLiteral("/run.sh"), "#!/bin/sh\necho \"hi\"\n");
    check(fm.richPreview(base + QStringLiteral("/run.sh")).value(QStringLiteral("language")).toString().contains(QStringLiteral("Bash")),
          QStringLiteral("a shell script is recognised"), fm.richPreview(base + QStringLiteral("/run.sh")).value(QStringLiteral("language")).toString());
    write(base + QStringLiteral("/readme.txt"), "just <words> & text\n");
    const QVariantMap txt = fm.richPreview(base + QStringLiteral("/readme.txt"));
    check(txt.value(QStringLiteral("kind")) == QStringLiteral("plain") && txt.value(QStringLiteral("html")).toString().contains(QStringLiteral("&lt;words&gt; &amp;")),
          QStringLiteral("plain text stays plain (and escaped)"), txt.value(QStringLiteral("html")).toString());
    return failures ? 1 : 0;
}
