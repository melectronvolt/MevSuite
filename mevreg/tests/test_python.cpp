// Scripts Python exécutés par l'interpréteur intégré ; mini-numpy comparé au vrai numpy.

#include "python/engine.h"

#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <thread>

using namespace Qt::StringLiterals;
using python::Engine;
using python::ScriptInput;
using python::ScriptOutput;

namespace {

ScriptOutput run(const QString &code, const ScriptInput &input = {})
{
    ScriptOutput out = Engine::instance().runSync(code, input);
    if (out.error && !code.contains(u"# erreur attendue"_s))
        qWarning().noquote() << "erreur inattendue :" << out.error->traceback << "\nsortie :" << out.stdoutText;
    return out;
}

} // namespace

class TestPython : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void miniPandas()
    {
        // colonnes, calculs qui ignorent les cases vides, filtres, lignes, statistiques, CSV
        const ScriptOutput out = run(uR"py(import numpy as np
import pandas as pd
df = pd.DataFrame({"t": [0, 1, 2, 3], "x": [1.0, 3.0, np.nan, 7.0]})
print(len(df), df.shape, df.columns)
print(round(df["x"].mean(), 4), df["x"].count(), round(df["x"].std(), 4))
df["v"] = df["x"] * 2
print(df.columns)
print(df[df["t"] > 1]["t"].tolist())
print(df.iloc[1]["x"], df.iloc[1:3].shape)
print(df.dropna().shape)
print(df.mean()["v"])
print(df.head(2).shape, df.tail(1)["t"].tolist())
print(df.rename(columns={"x": "pos"}).columns)
print(df.drop(columns=["v"]).columns)
print(df.to_csv(sep=";", decimal=",", index=False))
d = df.describe()
print(d.index, round(d["x"]["mean"], 4))
print(df)
print(pd.concat([df, df]).shape)
print(df.t.tolist())
)py"_s);
        QVERIFY2(!out.error, out.error ? qPrintable(out.error->traceback) : "");
        const QStringList l = out.stdoutText.split(u'\n');
        QCOMPARE(l.value(0), u"4 (4, 2) ['t', 'x']"_s);
        QCOMPARE(l.value(1), u"3.6667 3 3.0551"_s);
        QCOMPARE(l.value(2), u"['t', 'x', 'v']"_s);
        QCOMPARE(l.value(3), u"[2.0, 3.0]"_s);
        QCOMPARE(l.value(4), u"3.0 (2, 3)"_s);
        QCOMPARE(l.value(5), u"(3, 3)"_s);
        QVERIFY(l.value(6).startsWith(u"7.33333"_s));
        QCOMPARE(l.value(7), u"(2, 3) [3.0]"_s);
        QCOMPARE(l.value(8), u"['t', 'pos', 'v']"_s);
        QCOMPARE(l.value(9), u"['t', 'x']"_s);
        QCOMPARE(l.value(10), u"t;x;v"_s);
        QCOMPARE(l.value(11), u"0;1;2"_s);
        QCOMPARE(l.value(13), u"2;;"_s);
        QVERIFY(out.stdoutText.contains(u"['count', 'mean', 'std', 'min', '25%', '50%', '75%', 'max'] 3.6667"_s));
        QVERIFY(out.stdoutText.contains(u"(8, 3)"_s));
        QVERIFY(out.stdoutText.contains(u"[0.0, 1.0, 2.0, 3.0]"_s));
    }

    void listsAndArraysBecomeQuantities()
    {
        // une liste de nombres ou un tableau numpy devient une grandeur, sans unité ; grandeur()
        // l'emporte (avec son unité) ; ni les textes, ni les booléens ; une grandeur du document
        // changée dans le script n'est pas écrasée
        ScriptInput in;
        in.quantities.push_back({u"z"_s, u"m"_s, {1, 2, 3}});
        const ScriptOutput out = run(uR"py(import numpy as np
a = [1, 2, 3.5]
b = np.linspace(0, 1, 3)
noms = ["x", "y"]
masque = b > 0.5
grandeur("c", [1, 2], "V")
c = [5]
_cache = [1]
z = z * 2
)py"_s, in);
        QVERIFY2(!out.error, out.error ? qPrintable(out.error->traceback) : "");
        QStringList names;
        for (const auto &q : out.quantities)
            names << q.name;
        names.sort();
        QCOMPARE(names, (QStringList{u"a"_s, u"b"_s, u"c"_s}));
        for (const auto &q : out.quantities) {
            if (q.name == u"c") {
                QCOMPARE(q.unit, u"V"_s);
                QCOMPARE(q.values, (std::vector<double>{1, 2}));
            } else {
                QVERIFY(q.unit.isEmpty());
            }
        }
        // grandeur() laisse un repère à sa place parmi les ordres ; z, changée, est signalée
        QCOMPARE(out.commands.size(), 2);
        QCOMPARE(out.commands[0].toObject().value(u"ordre"_s).toString(), u"grandeur"_s);
        QCOMPARE(out.commands[0].toObject().value(u"nom"_s).toString(), u"c"_s);
        QCOMPARE(out.commands[1].toObject().value(u"ordre"_s).toString(), u"avertir"_s);
        QVERIFY(out.commands[1].toObject().value(u"texte"_s).toString().contains(u"« z »"_s));
    }

    void pandasReadCsvAndMevreg()
    {
        // read_csv devine les séparateurs (ici « ; » et la virgule) ; les unités des en-têtes sont gardées
        QTemporaryDir dir;
        QFile f(dir.filePath(u"m.csv"_s));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("t (s);U (V)\n0;1,5\n0,1;1,8\n0,2;2,1\n");
        f.close();
        ScriptInput in;
        in.folder = dir.path();
        in.quantities.push_back({u"z"_s, u"m"_s, {1, 2, 3}});
        const ScriptOutput out = run(uR"py(import pandas as pd
import mevreg as mr
df = pd.read_csv("m.csv")
print(df.columns, df.attrs["unites"], df["U (V)"].tolist())
noms = mr.importer(df, tableur="Mesures")
print(noms, mr.unite("U"), mr.valeurs("t").tolist())
print(mr.tableau().columns)
r = mr.modeliser("U", "t", "affine")
print(round(r.a, 6), round(r.b, 6), r.n)
mr.graphe("G", x="t", y="U")
mr.ouvrir("G")
)py"_s, in);
        QVERIFY2(!out.error, out.error ? qPrintable(out.error->traceback) : "");
        const QStringList l = out.stdoutText.split(u'\n');
        QCOMPARE(l.value(0), u"['t (s)', 'U (V)'] {'t (s)': 's', 'U (V)': 'V'} [1.5, 1.8, 2.1]"_s);
        QCOMPARE(l.value(1), u"['t', 'U'] V [0.0, 0.1, 0.2]"_s);
        QCOMPARE(l.value(2), u"['z', 't', 'U']"_s);
        QCOMPARE(l.value(3), u"3.0 1.5 3"_s);
        // grandeurs créées, ordres pour le logiciel dans l'ordre
        QCOMPARE(out.quantities.size(), size_t(2));
        QCOMPARE(out.quantities[1].unit, u"V"_s);
        QStringList orders;
        for (const QJsonValue v : out.commands)
            orders << v.toObject().value(u"ordre"_s).toString();
        QCOMPARE(orders, (QStringList{u"grandeur"_s, u"grandeur"_s, u"tableur"_s, u"modeliser"_s, u"graphe"_s, u"ouvrir"_s}));
        // une erreur claire pour une grandeur absente
        const ScriptOutput bad = run(u"import mevreg as mr\nmr.valeurs(\"absente\")  # erreur attendue\n"_s, in);
        QVERIFY(bad.error && bad.error->message.contains(u"absente"_s));
    }

    void sameOutputAsNumpy()
    {
        QFile f(QStringLiteral(TESTS_DIR "/numpy_reference.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject doc = QJsonDocument::fromJson(f.readAll()).object();
        const QString setup = doc.value(u"setup"_s).toString();
        const QJsonArray cases = doc.value(u"cases"_s).toArray();
        QJsonArray exprs;
        for (const QJsonValue c : cases)
            exprs.append(c.toObject().value(u"expr"_s));
        QString script = u"import numpy as np\n"_s + setup + u"\n_exprs = "_s + QString::fromUtf8(QJsonDocument(exprs).toJson(QJsonDocument::Compact)) + u'\n';
        script += u"for _e in _exprs:\n    try:\n        print(str(eval(_e)))\n    except Exception as _ex:\n        print('ERREUR ' + type(_ex).__name__ + ': ' + str(_ex))\n    print('§§')\n"_s;
        const ScriptOutput out = run(script);
        QVERIFY2(!out.error, out.error ? qPrintable(out.error->traceback) : "");
        const QStringList got = out.stdoutText.split(u"§§\n"_s);
        QStringList failures;
        for (qsizetype k = 0; k < cases.size(); ++k) {
            const QJsonObject c = cases[k].toObject();
            const QString expected = c.value(u"str"_s).toString();
            QString actual = k < got.size() ? got[k] : u"<absent>"_s;
            while (actual.endsWith(u'\n'))
                actual.chop(1);
            if (actual != expected)
                failures << u"%1\n   numpy  : %2\n   MeVReg : %3"_s.arg(c.value(u"expr"_s).toString(), expected, actual);
        }
        QVERIFY2(failures.isEmpty(), qPrintable(u"%1 écart(s) sur %2 cas :\n"_s.arg(failures.size()).arg(cases.size()) + failures.join(u'\n')));
    }

    void printAndNumpyBasics()
    {
        const ScriptOutput out = run(u"import numpy as np\n"
                                     "a = np.array([1, 2, 3])\n"
                                     "b = a * 2.5 + 1\n"
                                     "print(a, b, a.dtype, b.dtype)\n"
                                     "print(np.mean(b), np.std(b, ddof=1), b.max(), len(b))\n"
                                     "print(repr(np.linspace(0, 1, 5)))\n"
                                     "print(a[1:], a[-1], a[a > 1], b[[0, 2]])\n"_s);
        const QStringList lines = out.stdoutText.split(u'\n');
        QCOMPARE(lines[0], u"[1 2 3] [3.5 6.  8.5] int64 float64"_s);
        QCOMPARE(lines[1], u"6.0 2.5 8.5 3"_s);
        QCOMPARE(lines[2], u"array([0.  , 0.25, 0.5 , 0.75, 1.  ])"_s);
        QCOMPARE(lines[3], u"[2 3] 3 [2 3] [3.5 8.5]"_s);
    }

    void wrappingAndSummaryLikeNumpy()
    {
        const ScriptOutput out = run(u"import numpy as np\n"
                                     "print(np.linspace(0, 1, 10))\n"
                                     "print(repr(np.linspace(0, 1, 10)))\n"
                                     "print(np.arange(2000) / 7)\n"
                                     "print(repr(np.arange(2000) / 7))\n"
                                     "print(np.array([1+2.5j, 3-1j]))\n"_s);
        QCOMPARE(out.stdoutText,
                 u"[0.         0.11111111 0.22222222 0.33333333 0.44444444 0.55555556\n 0.66666667 0.77777778 0.88888889 1.        ]\n"
                 "array([0.        , 0.11111111, 0.22222222, 0.33333333, 0.44444444,\n       0.55555556, 0.66666667, 0.77777778, 0.88888889, 1.        ])\n"
                 "[0.00000000e+00 1.42857143e-01 2.85714286e-01 ... 2.85285714e+02\n 2.85428571e+02 2.85571429e+02]\n"
                 "array([0.00000000e+00, 1.42857143e-01, 2.85714286e-01, ...,\n       2.85285714e+02, 2.85428571e+02, 2.85571429e+02], shape=(2000,))\n"
                 "[1.+2.5j 3.-1.j ]\n"_s);
    }

    void quantitiesParametersAndGrandeur()
    {
        ScriptInput input;
        input.quantities = {{u"t"_s, u"s"_s, {0, 1, 2, 3}}, {u"x"_s, u"m"_s, {0, 1, 4, 9}}, {u"print"_s, {}, {1}}};
        input.parameters = {{u"m"_s, u"kg"_s, 2.0}};
        const ScriptOutput out = run(u"import numpy as np\n"
                                     "v = np.gradient(x, t)\n"
                                     "Ec = 0.5 * m * v**2\n"
                                     "grandeur(\"v\", v, \"m/s\")\n"
                                     "grandeur(\"Ec\", Ec, \"J\")\n"
                                     "grandeur(\"liste\", [1, 2, 3])\n"
                                     "print(v)\n"_s,
                                     input);
        QVERIFY(!out.error);
        QCOMPARE(out.stdoutText.trimmed(), u"[1. 2. 4. 5.]"_s);
        QCOMPARE(out.quantities.size(), size_t(3));
        QCOMPARE(out.quantities[0].name, u"v"_s);
        QCOMPARE(out.quantities[0].unit, u"m/s"_s);
        QCOMPARE(out.quantities[0].values, (std::vector<double>{1, 2, 4, 5}));
        QCOMPARE(out.quantities[1].values, (std::vector<double>{1, 4, 16, 25}));
        QCOMPARE(out.quantities[2].values, (std::vector<double>{1, 2, 3}));
    }

    void errorsReportTheLine()
    {
        ScriptOutput out = run(u"a = 1\nb = a + c  # erreur attendue\n"_s);
        QVERIFY(out.error);
        QCOMPARE(out.error->line, std::optional<int>(2));
        QVERIFY(out.error->message.contains(u"NameError"_s));
        out = run(u"x = (1, 2\nprint(x)  # erreur attendue\n"_s);
        QVERIFY(out.error && (out.error->message.contains(u"SyntaxError"_s) || out.error->traceback.contains(u"SyntaxError"_s)));
        // fonction absente du mini-numpy : message explicite
        out = run(u"import numpy as np\nnp.einsum('i', [1])  # erreur attendue\n"_s);
        QVERIFY2(out.error->message.contains(u"np.einsum n'est pas disponible dans ce logiciel"_s), qPrintable(out.error->message));
        QCOMPARE(out.error->line, std::optional<int>(2));
        out = run(u"n = input('n ?')  # erreur attendue\n"_s);
        QVERIFY(out.error->message.contains(u"paramètre"_s));
        // une erreur n'empêche pas de récupérer ce qui a été affiché avant
        out = run(u"print('avant')\n1/0  # erreur attendue\n"_s);
        QCOMPARE(out.stdoutText, u"avant\n"_s);
        QVERIFY(out.error->message.contains(u"ZeroDivisionError"_s));
    }

    void realNumpyIsNeverLoaded()
    {
        // même si numpy est installé sur la machine, c'est le mini-numpy qui répond
        const ScriptOutput out = run(u"import numpy, sys\nprint(numpy.__version__, 'site' in sys.modules)\n"_s);
        QCOMPARE(out.stdoutText.trimmed(), u"2.0-mevreg False"_s);
    }

    void matplotlibBuildsADescription()
    {
        ScriptInput input;
        input.quantities = {{u"t"_s, u"s"_s, {0, 0.1, 0.2}}};
        const ScriptOutput out = run(u"import numpy as np\n"
                                     "import matplotlib.pyplot as plt\n"
                                     "z = 1 - 4.9 * t**2\n"
                                     "plt.figure(\"Chute\")\n"
                                     "plt.errorbar(t, z, yerr=0.01, fmt='b+', label='mesures')\n"
                                     "plt.plot(t, 2 * z, 'r--')\n"
                                     "plt.title('Chute libre')\n"
                                     "plt.xlabel('t (s)')\n"
                                     "plt.legend()\n"
                                     "plt.grid()\n"
                                     "plt.show()\n"
                                     "fig, ax = plt.subplots(2, 1)\n"
                                     "ax[0].plot([1, 2, 3])\n"
                                     "ax2 = ax[1].twinx()\n"
                                     "ax2.plot(t, z, 'go')\n"_s,
                                     input);
        QVERIFY(!out.error);
        QCOMPARE(out.figures.size(), size_t(2));
        const auto &f = out.figures[0];
        QCOMPARE(f.desc.name, u"Chute"_s);
        const auto &ax = f.desc.axes[0];
        QCOMPARE(ax.title, std::optional<QString>(u"Chute libre"_s));
        QCOMPARE(ax.xlabel, std::optional<QString>(u"t (s)"_s));
        QVERIFY(ax.grid && ax.legend);
        QCOMPARE(ax.series.size(), size_t(2));
        const auto &s0 = ax.series[0];
        QCOMPARE(s0.xname, std::optional<QString>(u"t"_s));
        QCOMPARE(s0.yname, std::optional<QString>(u"z"_s));
        QCOMPARE(s0.marker, std::optional<QString>(u"+"_s));
        QCOMPARE(s0.color, std::optional<QString>(u"#0000ff"_s));
        QCOMPARE(s0.label, std::optional<QString>(u"mesures"_s));
        QCOMPARE(f.data[*s0.yerr], std::vector<double>{0.01});
        const auto &s1 = ax.series[1];
        QCOMPARE(s1.linestyle, std::optional<QString>(u"--"_s));
        QVERIFY(!s1.yname);
        QVERIFY(std::abs(f.data[s1.y][0] - 2.0) < 1e-12);
        const auto &g = out.figures[1];
        QCOMPARE(g.desc.rows, 2);
        QCOMPARE(g.desc.cols, 1);
        QCOMPARE(g.data[g.desc.axes[0].series[0].x], (std::vector<double>{0, 1, 2}));
        QVERIFY(g.desc.axes[1].series[0].right);
    }

    void typicalTpScripts()
    {
        // méthode d'Euler, chute avec frottements
        ScriptOutput out = run(u"import numpy as np\n"
                               "dt = 0.01\n"
                               "t = np.arange(0, 2, dt)\n"
                               "v = np.zeros(len(t))\n"
                               "for i in range(len(t) - 1):\n"
                               "    v[i+1] = v[i] + (9.81 - 0.5 * v[i]**2) * dt\n"
                               "grandeur('v_euler', v, 'm/s')\n"
                               "print(round(v[-1], 2))\n"_s);
        QCOMPARE(out.stdoutText.trimmed(), u"4.43"_s);
        // spectre d'un signal : pic à 440 Hz, même valeur que numpy
        out = run(u"import numpy as np\n"
                  "fe = 44000\n"
                  "t = np.arange(1000) / fe\n"
                  "s = 2 * np.sin(2 * np.pi * 440 * t)\n"
                  "A = np.abs(np.fft.rfft(s)) * 2 / len(s)\n"
                  "f = np.fft.rfftfreq(len(s), 1 / fe)\n"
                  "k = np.argmax(A)\n"
                  "print(f[k], round(A[k], 6))\n"_s);
        QCOMPARE(out.stdoutText.trimmed(), u"440.00000000000006 2.0"_s);
        // régression linéaire et incertitude de type A
        out = run(u"import numpy as np\n"
                  "x = np.array([0, 1, 2, 3])\n"
                  "y = np.array([1.1, 2.9, 5.2, 6.8])\n"
                  "a, b = np.polyfit(x, y, 1)\n"
                  "print(round(a, 2), round(b, 2))\n"
                  "g = np.array([9.81, 9.75, 9.86, 9.74, 9.79])\n"
                  "u = np.std(g, ddof=1) / np.sqrt(len(g))\n"
                  "print(f'{np.mean(g):.3f} {u:.3f}')\n"_s);
        QCOMPARE(out.stdoutText, u"1.94 1.09\n9.790 0.022\n"_s);
        // Monte-Carlo : tirages reproductibles
        out = run(u"import numpy as np\n"
                  "np.random.seed(1)\n"
                  "a = np.random.normal(10, 0.5, 20000)\n"
                  "np.random.seed(1)\n"
                  "b = np.random.normal(10, 0.5, 20000)\n"
                  "print(bool((a == b).all()), round(np.mean(a), 1), round(np.std(a), 1))\n"_s);
        QCOMPARE(out.stdoutText.trimmed(), u"True 10.0 0.5"_s);
    }

    void interruptAnInfiniteLoop()
    {
        Engine &e = Engine::instance();
        QSignalSpy spy(&e, &Engine::finished);
        const auto id = e.run(u"i = 0\nwhile True:\n    i += 1\n"_s, {});
        QElapsedTimer t;
        t.start();
        while (e.running() != id && t.elapsed() < 10000)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        QCOMPARE(e.running(), id);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        e.interrupt(id);
        QVERIFY(spy.wait(20000));
        QCOMPARE(spy.first().at(0).value<std::uint64_t>(), id);
        // l'interpréteur reste utilisable, et une interruption tardive n'affecte pas le script suivant
        e.interrupt(id);
        const ScriptOutput out = run(u"x = 2\nprint(x * 21)"_s);
        QVERIFY(!out.error);
        QCOMPARE(out.stdoutText, u"42\n"_s);
    }

    void scriptsAreIsolated()
    {
        run(u"secret = 12\nimport numpy as np\nnp.essai = 1"_s);
        const ScriptOutput out = run(u"print('secret' in globals())"_s);
        QCOMPARE(out.stdoutText, u"False\n"_s);
    }

    void drawingWindows()
    {
        ScriptInput input;
        input.parameters = {{u"D"_s, u"m"_s, 2.0}};
        const ScriptOutput out = run(u"import numpy as np\n"
                                     "import fenetre as fe\n"
                                     "x = np.linspace(-0.05, 0.05, 1001)\n"
                                     "a, l = 1e-4, 633e-9\n"
                                     "u = np.pi * a * x / (l * D)\n"
                                     "I = np.where(u == 0, 1.0, (np.sin(u) / np.where(u == 0, 1.0, u))**2)\n"
                                     "d = fe.Dessin('Fente', -0.05, 0.05, -0.02, 0.02, unite='m', fond='noir')\n"
                                     "d.bandes(I, palette='laser', longueur_onde=l)\n"
                                     "d.fleche(0, -0.01, l * D / a, -0.01, couleur='blanc')\n"
                                     "d.texte(0.002, -0.015, 'λD/a', couleur='blanc')\n"
                                     "d.montrer()\n"
                                     "c = fe.Dessin('Fentes', -0.02, 0.02, -0.01, 0.01, unite='m')\n"
                                     "c.carte('cos(pi*b*x/(l*D))^2*sinc(pi*a*x/(l*D))^2', palette='laser', longueur_onde=l, b=5e-4, a=a, l=l, nx=200)\n"
                                     "c.montrer()\n"
                                     "print(fe.couleur_longueur_onde(633e-9))\n"_s,
                                     input);
        QVERIFY2(!out.error, out.error ? qPrintable(out.error->traceback) : "");
        QCOMPARE(out.drawings.size(), size_t(2));
        const auto &d = out.drawings[0];
        QCOMPARE(d.desc.name, u"Fente"_s);
        QCOMPARE(d.desc.background, u"#000000"_s);
        QCOMPARE(d.desc.items.size(), size_t(3));
        QCOMPARE(d.data[0].size(), size_t(1001));
        const auto &c = out.drawings[1];
        QCOMPARE(c.data[0].size(), size_t(200 * 100));
        QCOMPARE(c.desc.items[0].type, drawing::Item::Map);
        QCOMPARE(c.desc.items[0].nx, 200);
        QCOMPARE(c.desc.items[0].ny, 100);
        QCOMPARE(out.stdoutText.trimmed(), u"#ff2f00"_s);
    }

    void identifiersSkipStringsCommentsAndAttributes()
    {
        const QStringList ids = python::identifiers(u"v = np.gradient(x, t)  # vitesse\nprint('t est le temps', v)\ns = \"\"\"z\"\"\""_s);
        QVERIFY(ids.contains(u"x"_s));
        QVERIFY(ids.contains(u"t"_s));
        QVERIFY(!ids.contains(u"gradient"_s));
        QVERIFY(!ids.contains(u"vitesse"_s));
        QVERIFY(!ids.contains(u"z"_s));
        QCOMPARE(python::scriptLine(u"Traceback (most recent call last):\n  File \"<script>\", line 3, in <module>\n  File \"numpy\", line 20, in f\nValueError: x"_s),
                 std::optional<int>(3));
    }
};

QTEST_GUILESS_MAIN(TestPython)
#include "test_python.moc"
