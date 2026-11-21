// Tests du code sans interface.

#include "analyzer.h"
#include "audio.h"
#include "signaux.h"
#include "dsp.h"
#include "fft.h"
#include "format.h"
#include "help.h"
#include "i18n.h"
#include "player.h"
#include "session.h"
#include "settings.h"
#include "wave.h"

#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <numbers>
#include <random>
#include <thread>

using namespace Qt::StringLiterals;
using namespace dsp;

namespace {

using essais::RATE;
using essais::sons_purs;

audio::Audio son_test()
{
    return audio::Audio::fabrique(u"signal"_s, RATE, {sons_purs({{440.0, 0.5}}, 3.0)}, false);
}

std::vector<std::pair<double, double>> serie_harmonique(double f0, const std::vector<double> &amplitudes)
{
    std::vector<std::pair<double, double>> out;
    for (size_t i = 0; i < amplitudes.size(); ++i) {
        if (amplitudes[i] > 0.0)
            out.emplace_back(double(i + 1) * f0, amplitudes[i]);
    }
    return out;
}

Analysis analyse_des_sons_purs(const std::vector<std::pair<double, double>> &components)
{
    const std::vector<float> x = sons_purs(components, 0.5);
    return *analyse(x, 0, x.size(), RATE, {Window::Hann, 0}, PeakParams{});
}

std::optional<double> fondamentale_de(const audio::Audio &a, audio::Channel channel)
{
    const Samples &signal = a.signal(channel);
    const auto r = analyse(*signal, 0, signal->size(), a.rate, {Window::Hann, 0}, PeakParams{});
    return r ? r->fundamental : std::nullopt;
}

Playback lecture(const std::vector<float> &samples, double from, size_t end, double rate, double outputRate)
{
    return Playback(std::make_shared<const std::vector<float>>(samples), rate, from, end, outputRate,
                    std::make_shared<PlaybackShared>());
}

/// Attend que `condition` soit vraie, en appelant `update` comme le fait l'interface.
void attend(Player &player, const std::function<bool(const Player &)> &condition)
{
    QElapsedTimer started;
    started.start();
    while (!condition(player)) {
        QVERIFY2(started.elapsed() < 5000, "attente trop longue");
        QCOMPARE(player.met_a_jour(), std::nullopt);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

QString lit_texte(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

} // namespace

class Tests : public QObject {
    Q_OBJECT

private slots:
    // ------------------------------------------------------------ dsp
    void fft_directe_et_inverse()
    {
        // L'impulsion placée au second échantillon décale la phase de chaque raie.
        fft::Plan plan(4);
        std::vector<fft::Complex> donnees{0.0, 1.0, 0.0, 0.0};
        plan.directe(donnees);
        const std::vector<fft::Complex> attendu{1.0, {0, -1}, -1.0, {0, 1}};
        for (size_t k = 0; k < attendu.size(); ++k)
            QVERIFY(std::abs(donnees[k] - attendu[k]) < 1e-12);
        plan.inverse(donnees);
        for (size_t i = 0; i < donnees.size(); ++i)
            QVERIFY(std::abs(donnees[i] - (i == 1 ? 4.0 : 0.0)) < 1e-12);
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, fft::Plan(3));
        donnees.resize(3);
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, plan.directe(donnees));
        QVERIFY_THROWS_EXCEPTION(std::length_error, fft::puissance_de_deux_suivante(std::numeric_limits<size_t>::max()));
    }

    void fenetres_ont_forme_attendue()
    {
        for (Window w : ALL_WINDOWS) {
            const std::vector<double> c = coefficients_fenetre(w, 1024);
            QCOMPARE(c.size(), size_t(1024));
            // symétrique autour du milieu, maximale au milieu
            QVERIFY(std::abs(c[1] - c[1023]) < 1e-12);
            QVERIFY(std::ranges::all_of(c, [&](double x) { return x <= c[512] + 1e-12; }));
            QCOMPARE(fenetre_du_code(code_fenetre(w)), w);
        }
        QVERIFY(std::abs(coefficients_fenetre(Window::Hann, 8)[0]) < 1e-12);
    }

    void sinus_amplitude_frequence_chaque_fenetre()
    {
        const std::vector<float> x = sons_purs({{1000.3, 0.5}}, 0.5);
        for (Window w : ALL_WINDOWS) {
            const auto sp = spectre(x, RATE, {w, 0});
            QVERIFY(sp);
            std::vector<Peak> p = pics(*sp, w, PeakParams{});
            // la fenêtre rectangulaire laisse voir des lobes secondaires : on garde le plus grand pic
            if (w != Window::Rectangular)
                QCOMPARE(p.size(), size_t(1));
            std::ranges::stable_sort(p, [](const Peak &a, const Peak &b) { return a.amplitude > b.amplitude; });
            // interpolation moins précise sur le lobe étroit de la fenêtre rectangulaire
            const double dfMax = w == Window::Rectangular ? 0.1 : 0.05;
            QVERIFY2(std::abs(p[0].freq - 1000.3) < dfMax, qPrintable(code_fenetre(w)));
            const double tolerance = w == Window::Rectangular ? 0.06 : 0.01;
            QVERIFY2(std::abs(p[0].amplitude - 0.5) < 0.5 * tolerance, qPrintable(code_fenetre(w)));
            // la fondamentale reste juste, même avec les lobes secondaires
            const auto f0 = fondamentale(p, sp->pas_frequence(), 20.0);
            QVERIFY(f0 && std::abs(*f0 - 1000.3) < dfMax);
        }
    }

    void amplitude_pic_entre_raies()
    {
        // une sinusoïde d'amplitude 0,5, placée tous les dixièmes de raie entre deux raies, avec ou
        // sans complétion par des zéros : l'amplitude du pic reste 0,5 à 0,5 % près, pour chaque fenêtre
        for (Window w : ALL_WINDOWS) {
            for (const auto &[samples, fftSize] : std::vector<std::pair<size_t, size_t>>{{8192, 8192}, {4096, 8192}}) {
                const double df = RATE / double(fftSize);
                for (int i = 0; i <= 10; ++i) {
                    const double f = (std::round(440.0 / df) + 0.1 * i) * df;
                    std::vector<float> x(samples);
                    for (size_t n = 0; n < samples; ++n)
                        x[n] = float(0.5 * std::sin(2.0 * std::numbers::pi * f * double(n) / RATE + 0.3));
                    const auto sp = spectre(x, RATE, {w, fftSize});
                    QVERIFY(sp);
                    std::vector<Peak> p = pics(*sp, w, PeakParams{});
                    QVERIFY(!p.empty());
                    std::ranges::stable_sort(p, [](const Peak &a, const Peak &b) { return a.amplitude > b.amplitude; });
                    const QString where = u"%1, %2 / %3, f = %4"_s.arg(code_fenetre(w)).arg(samples).arg(fftSize).arg(f);
                    QVERIFY2(std::abs(p[0].amplitude - 0.5) < 0.0025, qPrintable(where + u" : A = "_s + QString::number(p[0].amplitude)));
                    QVERIFY2(std::abs(p[0].freq - f) < 0.1 * df, qPrintable(where + u" : f = "_s + QString::number(p[0].freq)));
                }
            }
        }
    }

    void plateaux_a_pleine_echelle()
    {
        auto signal = sons_purs({{200.0, 1.0}}, 0.1);
        const auto propre = dsp::analyse(signal, 0, signal.size(), RATE, {}, {});
        QVERIFY(propre && !propre->plateaux);
        for (auto &x : signal)
            x = std::clamp(2 * x, -1.f, 1.f);
        const auto sature = dsp::analyse(signal, 0, signal.size(), RATE, {}, {});
        QVERIFY(sature && sature->plateaux);
    }

    void classement_des_pics_presque_egaux()
    {
        // À cette cadence, 1200 Hz tombe sur une raie, 440,5 Hz entre deux.
        // Le classement porte sur les amplitudes corrigées des deux sommets.
        for (double niveau : {0.4999, 0.5001}) {
            const auto son = sons_purs({{440.5, niveau}, {1200, 0.5}}, 0.5, 32768);
            const auto sp = spectre(son, 32768, {});
            QVERIFY(sp);
            PeakParams params;
            params.maxPeaks = 1;
            const auto p = pics(*sp, Window::Hann, params);
            QCOMPARE(p.size(), size_t(1));
            QVERIFY(std::abs(p[0].freq - (niveau > 0.5 ? 440.5 : 1200)) < 0.001);
        }
    }

    void pics_selectionnes_par_frequence_affinee()
    {
        // À cette cadence, les raies sont espacées de 1 Hz. Le pic est dans la
        // plage demandée, même si la raie la plus haute (440 Hz) est en dehors.
        constexpr double cadence = 8192.0;
        const auto son = sons_purs({{440.4, 0.5}}, 1.0, cadence);
        const auto spectreSon = spectre(son, cadence, {});
        QVERIFY(spectreSon);
        PeakParams plage;
        plage.fmin = 440.3;
        plage.fmax = 440.6;
        const auto trouves = pics(*spectreSon, Window::Hann, plage);
        QCOMPARE(trouves.size(), size_t(1));
        QVERIFY(std::abs(trouves.front().freq - 440.4) < 0.001);
        plage.fmin = 439.9;
        plage.fmax = 440.1;
        QVERIFY(pics(*spectreSon, Window::Hann, plage).empty());
    }

    void seuil_des_pics_garde_la_reference_hors_bande()
    {
        // Le 10 Hz n'est pas dans la plage affichée. Sa présence garde un seuil
        // de 0,008 dans ce spectre et évite de promouvoir un faible bruit à 200 Hz.
        constexpr double cadence = 8192.0;
        const auto son = sons_purs({{10.0, 0.8}, {200.0, 0.0001}}, 1.0, cadence);
        const auto spectreSon = spectre(son, cadence, {});
        QVERIFY(spectreSon);
        QVERIFY(pics(*spectreSon, Window::Hann, {}).empty());

        // À Nyquist, la raie n'a pas deux voisines pour l'interpolation.
        // Son amplitude de 0,8 fixe tout de même le seuil à 0,008.
        auto avecNyquist = sons_purs({{200.0, 0.006}}, 1.0, cadence);
        for (size_t i = 0; i < avecNyquist.size(); ++i)
            avecNyquist[i] += (i % 2 == 0 ? 0.8f : -0.8f);
        const auto spectreNyquist = spectre(avecNyquist, cadence, {Window::Rectangular, 0});
        QVERIFY(spectreNyquist);
        QVERIFY(pics(*spectreNyquist, Window::Rectangular, {}).empty());

        // Une sinusoïde placée sur la première raie admise reste dans la bande.
        const auto limite = sons_purs({{20.0, 0.8}}, 1.0, cadence);
        const auto spectreLimite = spectre(limite, cadence, {});
        QVERIFY(spectreLimite);
        const auto trouves = pics(*spectreLimite, Window::Hann, {});
        QCOMPARE(trouves.size(), size_t(1));
        QCOMPARE(trouves.front().freq, 20.0);
    }

    void seuil_des_pics_entre_raies()
    {
        // À −20 dB le seuil vaut 0,08 pour une dominante de 0,8. Placer l'un des
        // deux sons entre les raies réduit la hauteur de sa raie la plus forte.
        constexpr double cadence = 8192.0;
        PeakParams reglages;
        reglages.thresholdDb = -20;
        for (bool dominanteEntreRaies : {false, true}) {
            const double frequenceFaible = dominanteEntreRaies ? 400.0 : 400.5;
            const double amplitudeFaible = dominanteEntreRaies ? 0.072 : 0.095;
            const double frequenceForte = dominanteEntreRaies ? 3000.5 : 3000.0;
            const auto son = sons_purs({{frequenceFaible, amplitudeFaible}, {frequenceForte, 0.8}}, 1.0, cadence);
            const auto spectreSon = spectre(son, cadence, {Window::Rectangular, 0});
            QVERIFY(spectreSon);
            const auto trouves = pics(*spectreSon, Window::Rectangular, reglages);
            QCOMPARE(trouves.size(), dominanteEntreRaies ? size_t(1) : size_t(2));
            QVERIFY(std::abs(trouves.back().freq - frequenceForte) < 0.01);
        }
    }

    void amplitude_sur_des_raies_tres_serrees()
    {
        // À 128 échantillons par seconde, cet extrait donne des raies distantes
        // de moins d'un millième de hertz. Le sommet est presque à mi-chemin.
        constexpr double cadence = 128, duree = 1024;
        const double frequence = 40 + 0.45 / duree;
        const auto son = sons_purs({{frequence, 0.5}}, duree, cadence);
        const auto sp = spectre(son, cadence, {Window::Rectangular, 0});
        QVERIFY(sp);
        PeakParams params;
        params.thresholdDb = -6;
        const auto p = pics(*sp, Window::Rectangular, params);
        QCOMPARE(p.size(), size_t(1));
        QVERIFY(std::abs(p.front().amplitude - 0.5) < 0.0001);
    }

    void tailles_spectre_et_moyennage()
    {
        const std::vector<float> x = sons_purs({{440.0, 0.8}}, 1.0);
        // automatique : tout l'extrait, complété jusqu'à la puissance de 2 suivante
        const auto autoSize = spectre(x, RATE, {Window::Hann, 0});
        QCOMPARE(autoSize->fftLen, size_t(65536));
        QCOMPARE(autoSize->segmentLen, size_t(44100));
        QCOMPARE(autoSize->segments, size_t(1));
        QCOMPARE(autoSize->amplitude.size(), size_t(32769));
        // taille fixée plus petite que l'extrait : segments moyennés, recouverts de moitié
        const auto welch = spectre(x, RATE, {Window::Hann, 4096});
        QCOMPARE(welch->fftLen, size_t(4096));
        QCOMPARE(welch->segmentLen, size_t(4096));
        QCOMPARE(welch->segments, size_t((44100 - 4096 + 2047) / 2048 + 1));
        const std::vector<Peak> p = pics(*welch, Window::Hann, PeakParams{});
        QVERIFY(std::abs(p[0].freq - 440.0) < 0.5);
        QVERIFY(std::abs(p[0].amplitude - 0.8) < 0.02);
        QVERIFY(std::abs(welch->pas_frequence() - RATE / 4096.0) < 1e-9);
        QVERIFY(!spectre(std::span(x).first(4), RATE, {Window::Hann, 0}));
        QVERIFY(!spectre(x, RATE, {Window::Hann, 1000}));
        QVERIFY(!spectre(x, RATE, {Window::Hann, 1}));
        const std::atomic_bool annule = true;
        QVERIFY(!spectre(x, RATE, {}, &annule));
        QVERIFY(pics(*welch, Window::Hann, {}, &annule).empty());
        // la composante continue est retirée
        std::vector<float> offset = x;
        for (float &v : offset)
            v += 0.3f;
        QVERIFY(spectre(offset, RATE, {Window::Hann, 0})->amplitude[0] < 1e-6);
    }

    void note_uniquement_en_fin_extrait()
    {
        constexpr double rate = 48000;
        std::vector<float> x(312000, 0.0f);
        for (size_t i = 288000; i < x.size(); ++i)
            x[i] = float(0.6 * std::sin(2 * std::numbers::pi * 440 * double(i) / rate));
        const auto a = analyse(x, 0, x.size(), rate, {}, {});
        QVERIFY(a && a->fundamental && a->period);
        QCOMPARE(a->spectrum.segments, size_t(2));
        QVERIFY(std::abs(*a->fundamental - 440) < 0.3);
        QVERIFY(std::abs(1 / *a->period - 440) < 1.0);
    }

    void frequences_proches_separees()
    {
        // battements 440 et 446 Hz : séparés avec 1 s d'extrait (finesse ≈ 1 Hz)
        const std::vector<float> x = sons_purs({{440.0, 0.5}, {446.0, 0.5}}, 1.0);
        const auto sp = spectre(x, RATE, {Window::Hann, 0});
        const std::vector<Peak> p = pics(*sp, Window::Hann, PeakParams{});
        QCOMPARE(p.size(), size_t(2));
        QVERIFY(std::abs(p[0].freq - 440.0) < 0.1 && std::abs(p[1].freq - 446.0) < 0.1);
    }

    void battements_sans_fondamentale_dans_la_plage()
    {
        // Le motif entier revient toutes les 250 ms. Ses deux composantes sont
        // visibles, mais sa fondamentale de 4 Hz est sous la limite de 20 Hz.
        const auto son = sons_purs({{440, 0.35}, {444, 0.35}}, 4.0);
        const auto a = analyse(son, 0, son.size(), RATE, {}, {});
        QVERIFY(a);
        QCOMPARE(a->peaks.size(), size_t(2));
        QVERIFY(std::abs(a->peaks[0].freq - 440) < 0.05);
        QVERIFY(std::abs(a->peaks[1].freq - 444) < 0.05);
        QVERIFY(!a->fundamental);
        QVERIFY(!a->period);

        // Sur la demi-seconde proposée à l'ouverture, les pics peuvent se
        // confondre. La phase s'inverse quand même au creux du battement.
        const auto court = sons_purs({{440, 0.35}, {444, 0.35}}, 1.0, 48000);
        const auto demiSeconde = analyse(court, 23040, 47040, 48000, {}, {});
        QVERIFY(demiSeconde && !demiSeconde->peaks.empty());
        QVERIFY(!demiSeconde->fundamental && !demiSeconde->period);

        // Une note de 440 Hz dont l'amplitude oscille légèrement à 4 Hz présente
        // aussi deux pics voisins, beaucoup plus faibles que la note.
        const auto note = sons_purs({{436, 0.05}, {440, 0.5}, {444, 0.05}}, 2.0);
        const auto mesure = analyse(note, 0, note.size(), RATE, {}, {});
        QVERIFY(mesure && mesure->fundamental && mesure->period);
        QVERIFY(std::abs(*mesure->fundamental - 440) < 0.1);
        QVERIFY(std::abs(1 / *mesure->period - 440) < 0.5);
    }

    void note_avec_vibrato()
    {
        // Cinq oscillations de hauteur par seconde, à ±4 Hz autour du la.
        std::vector<float> son(96000);
        for (size_t i = 0; i < son.size(); ++i) {
            const double t = double(i) / 48000;
            const double phase = 2 * std::numbers::pi * 440 * t + 0.8 * std::sin(2 * std::numbers::pi * 5 * t);
            son[i] = float(0.5 * std::sin(phase));
        }
        for (size_t fin : {size_t(24000), son.size()}) {
            const auto a = analyse(son, 0, fin, 48000, {}, {});
            QVERIFY(a && a->fundamental && a->period);
            QVERIFY(std::abs(*a->fundamental - 440) < 4);
            QVERIFY(std::abs(1 / *a->period - 440) < 4);
        }
    }

    void fondamentale_des_sons_musicaux()
    {
        // flûte : tous les harmoniques
        auto a = analyse_des_sons_purs(serie_harmonique(440.0, {1.0, 0.4, 0.25, 0.1, 0.05}));
        QVERIFY(std::abs(a.fundamental.value() - 440.0) < 0.05);
        // clarinette : harmoniques impairs
        a = analyse_des_sons_purs(serie_harmonique(147.0, {1.0, 0.0, 0.6, 0.0, 0.35, 0.0, 0.2}));
        QVERIFY(std::abs(a.fundamental.value() - 147.0) < 0.05);
        // harmonique 2 plus fort que la fondamentale
        a = analyse_des_sons_purs(serie_harmonique(110.0, {0.3, 1.0, 0.5, 0.4, 0.2}));
        QVERIFY(std::abs(a.fundamental.value() - 110.0) < 0.05);
        // fondamentale absente (seuls les harmoniques 2 à 5)
        a = analyse_des_sons_purs(serie_harmonique(200.0, {0.0, 1.0, 0.8, 0.6, 0.4}));
        QVERIFY(std::abs(a.fundamental.value() - 200.0) < 0.05);
        // son pur
        a = analyse_des_sons_purs({{523.25, 0.5}});
        QVERIFY(std::abs(a.fundamental.value() - 523.25) < 0.05);
        // période par autocorrélation
        a = analyse_des_sons_purs(serie_harmonique(220.0, {1.0, 0.5, 0.3}));
        QVERIFY(std::abs(a.period.value() - 1.0 / 220.0) < 1e-6);
    }

    void tableau_des_harmoniques()
    {
        const Analysis a = analyse_des_sons_purs(serie_harmonique(200.0, {1.0, 0.5, 0.0, 0.25}));
        const std::vector<Harmonic> h = harmoniques(a.peaks, a.fundamental.value(), a.spectrum.pas_frequence(), 5);
        QCOMPARE(h.size(), size_t(5));
        QVERIFY(h[0].peak && h[1].peak && h[3].peak);
        QVERIFY(!h[2].peak && !h[4].peak);
        QVERIFY(std::abs(h[1].peak->amplitude - 0.5) < 0.01);
        QVERIFY(std::abs(h[3].expected - 800.0) < 0.1);
    }

    void periode_des_sons_aigus()
    {
        constexpr double cadence = 48000;
        for (double frequence : {6000., 8000., 11300., 18000., 21700.}) {
            const auto son = sons_purs({{frequence, 0.5}}, 0.5, cadence);
            const auto a = analyse(son, 0, son.size(), cadence, {}, {});
            QVERIFY(a && a->fundamental && a->period);
            QVERIFY(std::abs(*a->fundamental - frequence) < 0.1);
            QVERIFY2(std::abs(1 / *a->period - frequence) < 0.005 * frequence, qPrintable(QString::number(frequence)));
        }
    }

    void fondamentale_avec_deux_rangs_non_consecutifs()
    {
        for (double amplitude : {0.3, 0.6}) {
            // En changeant le pic dominant, on essaie les rangs 2 et 5 dans les deux sens.
            const auto a = analyse_des_sons_purs({{200, amplitude}, {500, 0.45}});
            QVERIFY(a.fundamental && a.period);
            QVERIFY(std::abs(*a.fundamental - 100) < 0.1);
            QVERIFY(std::abs(1 / *a.period - 100) < 0.1);
            const auto rangs = harmoniques(a.peaks, *a.fundamental, a.spectrum.pas_frequence(), 5);
            QVERIFY(!rangs[0].peak && rangs[1].peak && !rangs[2].peak && !rangs[3].peak && rangs[4].peak);
        }
    }

    void un_pic_compte_pour_un_seul_rang()
    {
        constexpr double cadence = 48000;
        constexpr double frequence = cadence / 1024;
        const auto son = sons_purs({{frequence, 0.5}}, 1, cadence);
        const auto a = analyse(son, 0, son.size(), cadence, {Window::Hann, 1024}, {});
        QVERIFY(a && a->fundamental);
        const auto rangs = harmoniques(a->peaks, *a->fundamental, a->spectrum.pas_frequence(), 3);
        QVERIFY(rangs[0].peak);
        QVERIFY(!rangs[1].peak && !rangs[2].peak);
    }

    void note_breve_entouree_de_silence()
    {
        constexpr int cadence = 48000;
        auto note = sons_purs({{440, 0.5}}, 0.1, cadence);
        const auto enveloppe = coefficients_fenetre(Window::Hann, note.size());
        for (size_t i = 0; i < note.size(); ++i)
            note[i] *= float(enveloppe[i]);
        for (size_t debut : {234500u, 237700u}) {
            std::vector<float> son(10 * cadence);
            std::ranges::copy(note, son.begin() + debut);
            const auto mesure = analyse(son, 0, son.size(), cadence, {}, {});
            QVERIFY(mesure && mesure->fundamental && mesure->period);
            QVERIFY(std::abs(*mesure->fundamental - 440) < 1);
            QVERIFY(std::abs(*mesure->period * 440 - 1) < 0.01);
        }
        // Deux notes de même force séparées par du silence ne forment pas une seule note stable.
        std::vector<float> son(10 * cadence);
        std::ranges::copy(note, son.begin() + cadence);
        auto autre = sons_purs({{587, 0.5}}, 0.1, cadence);
        for (size_t i = 0; i < autre.size(); ++i)
            son[8 * cadence + i] = autre[i] * float(enveloppe[i]);
        const auto mesure = analyse(son, 0, son.size(), cadence, {}, {});
        QVERIFY(mesure && !mesure->fundamental && !mesure->period);
    }

    void silence_sans_fondamentale()
    {
        const std::vector<float> silence(4096, 0.0f);
        const auto a = analyse(silence, 0, 4096, RATE, {Window::Hann, 0}, PeakParams{});
        QVERIFY(a && a->peaks.empty() && !a->fundamental && !a->period);
        QVERIFY(!analyse(silence, 10, 4097, RATE, {Window::Hann, 0}, PeakParams{}));
    }

    void bruits_et_balayage_sans_periodicite_stable()
    {
        std::vector<float> signal(48000);
        for (unsigned seed : {7, 302, 7103}) {
            std::mt19937 generateur(seed);
            std::uniform_real_distribution<float> bruit(-0.5f, 0.5f);
            for (float &v : signal)
                v = bruit(generateur);
            const auto a = analyse(signal, 0, signal.size(), 48000, {}, {});
            QVERIFY(a && !a->peaks.empty() && !a->fundamental && !a->period);
        }
        for (const auto &[debut, fin] : {std::pair{200.0, 2000.0}, std::pair{350.0, 450.0}}) {
            for (size_t i = 0; i < signal.size(); ++i) {
                const double t = double(i) / 48000;
                signal[i] = float(std::sin(2 * std::numbers::pi * (debut * t + 0.5 * (fin - debut) * t * t)));
            }
            const auto a = analyse(signal, 0, signal.size(), 48000, {}, {});
            QVERIFY(a && !a->peaks.empty() && !a->fundamental && !a->period);
        }
    }

    void passages_par_zero()
    {
        const std::vector<float> x = sons_purs({{100.0, 1.0}}, 0.1);
        // passages montants à 0, 10 ms, 20 ms…
        const auto t = passage_par_zero_montant(x, RATE, 0.0102, 0.002);
        QVERIFY(t && std::abs(*t - 0.010) < 1e-6);
        QVERIFY(!passage_par_zero_montant(x, RATE, 0.015, 0.001));

        // À 1 kHz, ce passage se produit à 0,2 ms. Le rayon de recherche
        // peut être inférieur à un échantillon lorsque le graphe est agrandi.
        const std::vector<float> rampe{-0.2f, 0.8f};
        const auto passage = passage_par_zero_montant(rampe, 1000, 0.0002, 0.00003);
        QVERIFY(passage && std::abs(*passage - 0.0002) < 1e-9);
        QVERIFY(!passage_par_zero_montant(rampe, 1000, 0.00045, 0.00007));
        const std::vector<float> zeroExact{-1, 0, 1};
        QCOMPARE(passage_par_zero_montant(zeroExact, 1000, 0.001, 0.0), std::optional(0.001));
    }

    // ------------------------------------------------------------ tracé du signal
    void enveloppe_egale_parcours_direct()
    {
        std::vector<float> samples(100'003);
        for (size_t i = 0; i < samples.size(); ++i)
            samples[i] = std::sin(float(i) * 0.37f) * float(i % 997) / 997.0f;
        const Envelope env(samples);
        const auto scan = [&](size_t first, size_t last) {
            const auto [lo, hi] = std::minmax_element(samples.begin() + qsizetype(first), samples.begin() + qsizetype(last));
            return std::pair(*lo, *hi);
        };
        for (const auto &[first, last] : std::vector<std::pair<size_t, size_t>>{
                 {0, 10}, {5, 70'000}, {123, 100'003}, {64, 128}, {63, 65}, {127, 321},
                 {0, 65'536}, {65'535, 65'538}, {0, 100'003}, {99'000, 100'003}})
            QCOMPARE(env.min_max(samples, first, last), scan(first, last));
        // peu d'échantillons visibles : les échantillons eux-mêmes
        const double rate = 1000.0;
        std::vector<QPointF> pts = env.points(samples, rate, 1.0, 1.1, 800);
        QVERIFY(pts.size() <= 103);
        for (size_t i = 1; i < pts.size(); ++i)
            QVERIFY(pts[i].x() > pts[i - 1].x());
        // beaucoup : deux points par colonne, qui encadrent le signal
        pts = env.points(samples, rate, 0.0, 100.0, 500);
        QCOMPARE(pts.size(), size_t(1000));
        const auto [lo, hi] = scan(0, samples.size());
        double ymin = INFINITY, ymax = -INFINITY;
        for (const QPointF &p : pts) {
            ymin = std::min(ymin, p.y());
            ymax = std::max(ymax, p.y());
        }
        QCOMPARE(ymin, double(lo));
        QCOMPARE(ymax, double(hi));
        QVERIFY(env.points(samples, rate, 200.0, 300.0, 500).empty());
        QVERIFY(Envelope({}).points({}, rate, 0.0, 1.0, 10).empty());
        // Le pic juste après le bord droit ne doit pas fixer l'échelle de toute la vue.
        std::vector<float> impulsion(1002, 0.0f);
        impulsion[1000] = 1.0f;
        const Envelope bref(impulsion);
        const auto vue = bref.points(impulsion, rate, 0.0, 0.999, 20);
        QCOMPARE(std::ranges::max(vue, {}, &QPointF::y).y(), 0.0);
        const auto bord = bref.points(impulsion, rate, 0.0, 0.9995, 20);
        QVERIFY(std::abs(bord.back().y() - 0.5) < 1e-9);
    }

    void enveloppe_conserve_le_dernier_echantillon()
    {
        // 30 / 11 colonnes donne un quotient arrondi. Le dernier pic doit rester visible.
        std::vector<float> signal(30, 0.f);
        signal.back() = 1.f;
        const auto points = Envelope(signal).points(signal, 1.0, 0.0, 29.0, 11);
        QCOMPARE(points.size(), size_t(22));
        QCOMPARE(points.back().y(), 1.0);
    }

    // ------------------------------------------------------------ nombres
    void nombres()
    {
        QCOMPARE(texte_nombre(440.123, 4, u','), u"440,1"_s);
        QCOMPARE(texte_nombre(0.0022727, 4, u'.'), u"0.002273"_s);
        QCOMPARE(texte_nombre(9.9996, 4, u','), u"10,00"_s);
        QCOMPARE(texte_nombre(12345.6, 3, u','), u"12300"_s);
        QCOMPARE(texte_nombre(-999.6, 3, u','), u"−1000"_s);
        QCOMPARE(texte_nombre(0.0099996, 3, u','), u"0,0100"_s);
        QCOMPARE(texte_decimal(-12.34, 1, u','), u"−12,3"_s);
        QCOMPARE(texte_decimal(-0.01, 1, u','), u"0,0"_s);
        QCOMPARE(nombre_csv(-12.34, 2, u','), u"-12,34"_s);
        QCOMPARE(texte_temps(0.0022727, u','), u"2,273 ms"_s);
        QCOMPARE(texte_temps(1.5, u'.'), u"1.5000 s"_s);
        QCOMPARE(texte_frequence(440.04, u','), u"440,0 Hz"_s);
        QCOMPARE(texte_frequence(55.123, u'.'), u"55.12 Hz"_s);
        QCOMPARE(texte_frequence(12'345.6, u','), u"12346 Hz"_s);
        QCOMPARE(texte_frequence_court(2000.0, u','), u"2 kHz"_s);
        QCOMPARE(texte_frequence_court(1500.0, u','), u"1,5 kHz"_s);
        QCOMPARE(texte_frequence_court(50.0, u'.'), u"50 Hz"_s);
        QCOMPARE(texte_position(0.73, 48000, u','), u"0,73000 s"_s);
        QCOMPARE(texte_duree(3.2, u','), u"3,20 s"_s);
        QCOMPARE(texte_duree(125.4, u','), u"2 min 05 s"_s);
        QCOMPARE(texte_duree(0.85, u'.'), u"850 ms"_s);
        QVERIFY(std::abs(en_decibels(0.5) + 6.0206) < 1e-3);
        QCOMPARE(plus_court(-55.5), u"-55.5"_s);
        QCOMPARE(plus_court(5000.0), u"5000"_s);
    }

    // ------------------------------------------------------------ lecture des sons
    void prise_micro_et_sauvegarde()
    {
        QAudioFormat format;
        format.setSampleRate(32000);
        format.setSampleFormat(QAudioFormat::Int16);
        format.setChannelCount(2);
        QByteArray pcm;
        for (int i = 0; i < 32000; ++i) {
            const qint16 v = qint16(16000 * std::sin(2 * std::numbers::pi * 440 * i / 32000));
            pcm.append(reinterpret_cast<const char *>(&v), sizeof(v));
            pcm.append(reinterpret_cast<const char *>(&v), sizeof(v));
        }
        audio::Prise vide(format);
        QVERIFY(!vide.termine(u"vide"_s));
        audio::Prise prise(format);
        for (int i = 0; i < pcm.size(); i += 513)
            prise.ajoute(pcm.mid(i, 513));
        QCOMPARE(prise.duree(), 1.0);
        QVERIFY(std::abs(prise.releve_crete() - 16000.f / 32768) < 1e-4);
        QCOMPARE(prise.releve_crete(), 0.f);
        auto son = prise.termine(u"prise"_s);
        QVERIFY(son);
        QCOMPARE(son->rate, 32000.0);
        QCOMPARE(son->channels.size(), size_t(2));
        QTemporaryDir dir;
        QVERIFY(!audio::ecrit_wav(dir.path(), *son));
        const QString fichier = dir.filePath(u"prise.wav"_s);
        QVERIFY(audio::ecrit_wav(fichier, *son));
        auto relu = audio::charge(fichier);
        QVERIFY(relu.audio);
        QCOMPARE(relu.audio->longueur(), son->longueur());
        auto f0 = fondamentale_de(*relu.audio, audio::Channel::Mix);
        QVERIFY(f0 && std::abs(*f0 - 440) < 0.2);
        format.setSampleRate(10);
        audio::Prise longue(format);
        longue.ajoute(pcm);
        QVERIFY(longue.pleine());
        QCOMPARE(longue.duree(), audio::MAX_SECONDS);
    }

    void lit_wav_stereo()
    {
        QTemporaryDir dir;
        std::vector<float> left(22050), right(22050);
        for (size_t i = 0; i < left.size(); ++i) {
            left[i] = float(0.5 * std::sin(2.0 * std::numbers::pi * 440.0 * double(i) / 22050.0));
            right[i] = float(0.5 * std::sin(2.0 * std::numbers::pi * 660.0 * double(i) / 22050.0));
        }
        const QString path = dir.filePath(u"stéréo test.wav"_s);
        QVERIFY(audio::ecrit_wav(path, 22050, {left, right}));
        const audio::LoadResult r = audio::charge(path);
        QVERIFY(r.audio);
        const audio::Audio &a = *r.audio;
        QCOMPARE(a.name, u"stéréo test.wav"_s);
        QCOMPARE(a.rate, 22050.0);
        QCOMPARE(a.channels.size(), size_t(2));
        QCOMPARE(a.longueur(), size_t(22050));
        QVERIFY(std::abs(a.duree() - 1.0) < 1e-9);
        QVERIFY(std::abs(fondamentale_de(a, audio::Channel::Left).value() - 440.0) < 0.05);
        QVERIFY(std::abs(fondamentale_de(a, audio::Channel::Right).value() - 660.0) < 0.05);
        QVERIFY(std::abs(a.crete() - 0.5) < 0.01);
    }

    void wav_voies_identiques_ou_divergentes_en_fin_de_fichier()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"stereo.wav"_s);
        std::vector<float> gauche(96000, 0.5f), droite = gauche;
        for (bool identiques : {true, false}) {
            if (!identiques)
                droite.back() = -0.5f;
            QVERIFY(audio::ecrit_wav(path, 48000, {gauche, droite}));
            const auto r = audio::charge(path);
            QVERIFY(r.audio);
            const auto &a = *r.audio;
            QCOMPARE(a.channels.size(), size_t(2));
            QCOMPARE(a.longueur(), gauche.size());
            QCOMPARE(a.channels[0] == a.channels[1], identiques);
            QCOMPARE(a.channels[0] == a.mix, identiques);
            QCOMPARE(a.channels[1]->front(), 0.5f);
            QCOMPARE(a.channels[1]->back(), droite.back());
            QCOMPARE(a.mix->back(), identiques ? 0.5f : 0.0f);
        }
    }

    void sauvegarde_pcm16_fidele()
    {
        QAudioFormat format;
        format.setSampleRate(48000);
        format.setChannelCount(1);
        format.setSampleFormat(QAudioFormat::Int16);
        QByteArray pcm;
        for (int code = -32768; code <= 32767; ++code) {
            const qint16 valeur = qint16(code);
            pcm.append(reinterpret_cast<const char *>(&valeur), sizeof(valeur));
        }
        audio::Prise prise(format);
        prise.ajoute(pcm);
        const auto son = prise.termine(u"prise"_s);
        QVERIFY(son);
        QTemporaryDir dir;
        const auto path = dir.filePath(u"prise.wav"_s);
        QVERIFY(audio::ecrit_wav(path, *son));
        const auto relu = audio::charge(path);
        QVERIFY(relu.audio);
        QCOMPARE(relu.audio->longueur(), size_t(65536));
        QVERIFY(*relu.audio->mix == *son->mix);

        // Le débit d'un WAV tient sur 32 bits. Refuser une cadence qui le dépasse
        // laisse intacte la prise déjà sauvegardée.
        QVERIFY(!audio::ecrit_wav(path, std::numeric_limits<int>::max(), {{0.f}, {0.f}}));
        const auto conserve = audio::charge(path);
        QVERIFY(conserve.audio);
        QCOMPARE(conserve.audio->longueur(), size_t(65536));

        // Le PCM signé va jusqu'à +32767. Une entrée à +1 doit rester positive.
        QVERIFY(audio::ecrit_wav(path, 48000, {{-1.5f, -1.0f, 0.0f, 1.0f, 1.5f}}));
        const auto bornes = audio::charge(path);
        QVERIFY(bornes.audio);
        const float maximum = 32767.f / 32768;
        QCOMPARE(*bornes.audio->mix, (std::vector<float>{-1, -1, 0, maximum, maximum}));
    }

    void wav_extensible_verifie_le_sous_format()
    {
        QByteArray donnees;
        QBuffer fichier(&donnees);
        QVERIFY(fichier.open(QIODevice::ReadWrite));
        QDataStream entete(&fichier);
        entete.setByteOrder(QDataStream::LittleEndian);
        entete.writeRawData("RIFF", 4);
        entete << quint32(62);
        entete.writeRawData("WAVEfmt ", 8);
        entete << quint32(40) << quint16(0xfffe) << quint16(1) << quint32(8000) << quint32(16000)
               << quint16(2) << quint16(16);
        entete << quint16(22) << quint16(16) << quint32(0);
        const QByteArray pcm = QByteArray::fromHex("0100000000001000800000aa00389b71");
        const auto positionIdentifiant = fichier.pos();
        entete.writeRawData(pcm.constData(), pcm.size());
        entete.writeRawData("data", 4);
        entete << quint32(2) << qint16(16384);

        QVERIFY(fichier.seek(0));
        const auto lecture = audio::lit_wav(fichier, u"PCM extensible"_s);
        QVERIFY(lecture && lecture->audio);
        QCOMPARE(lecture->audio->mix->front(), 0.5f);

        // Un sous-format inconnu peut partager le même numéro sur les deux premiers octets.
        // Le lecteur laisse alors la main aux autres décodeurs.
        QVERIFY(fichier.seek(positionIdentifiant + 15));
        QCOMPARE(fichier.write("x", 1), 1);
        QVERIFY(fichier.seek(0));
        QVERIFY(!audio::lit_wav(fichier, u"Autre sous-format"_s));
    }

    void wav_bloc_format_etendu_et_annulation()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"format-etendu.wav"_s);
        QFile fichier(path);
        QVERIFY(fichier.open(QIODevice::WriteOnly));
        QDataStream wav(&fichier);
        wav.setByteOrder(QDataStream::LittleEndian);
        // Les champs PCM sont suivis de 25 octets supplémentaires et d'un octet de bourrage.
        wav.writeRawData("RIFF", 4);
        wav << quint32(64);
        wav.writeRawData("WAVEfmt ", 8);
        wav << quint32(41) << quint16(1) << quint16(1) << quint32(8000) << quint32(16000) << quint16(2) << quint16(16);
        wav.writeRawData(QByteArray(26, 0).constData(), 26);
        wav.writeRawData("data", 4);
        wav << quint32(2) << qint16(16384);
        fichier.close();
        const std::atomic_bool annule = true;
        QVERIFY(!audio::charge(path, &annule).audio);
        const auto r = audio::charge(path);
        QVERIFY(r.audio);
        QCOMPARE(r.audio->longueur(), size_t(1));
        QCOMPARE(r.audio->crete(), 0.5f);
    }

    void wav_metadonnees_apres_donnees_sans_son()
    {
        // bloc LIST (métadonnées) après les échantillons, comme l'écrivent beaucoup d'enregistreurs :
        // seuls les échantillons déclarés sont lus
        QTemporaryDir dir;
        const QString path = dir.filePath(u"avec-list.wav"_s);
        QVERIFY(audio::ecrit_wav(path, 8000, {std::vector<float>(100, 0.0f)}));
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QByteArray bytes = f.readAll();
        bytes += QByteArray("LIST", 4);
        const quint32 listSize = 8;
        bytes += QByteArray(reinterpret_cast<const char *>(&listSize), 4);
        bytes += QByteArray("INFOABCD", 8);
        const quint32 riffSize = quint32(bytes.size() - 8);
        bytes.replace(4, 4, QByteArray(reinterpret_cast<const char *>(&riffSize), 4));
        f.seek(0);
        f.write(bytes);
        f.close();
        const audio::LoadResult r = audio::charge(path);
        QVERIFY(r.audio);
        QCOMPARE(r.audio->longueur(), size_t(100));
        QCOMPARE(r.audio->crete(), 0.0f);
    }

    /// Fichier WAV fait à la main : en-tête « fmt » (`tag` 1 : entiers, 3 : flottants), bloc « data »
    /// de taille annoncée `declared`, ses octets `data`, puis `after`.
    static QString wav_brut(const QTemporaryDir &dir, const QString &name, quint16 tag, quint16 bits, quint32 declared,
                          const QByteArray &data, const QByteArray &after = {})
    {
        const auto u16 = [](quint16 v) { return QByteArray(reinterpret_cast<const char *>(&v), 2); };
        const auto u32 = [](quint32 v) { return QByteArray(reinterpret_cast<const char *>(&v), 4); };
        const quint32 rate = 8000;
        const quint16 block = bits / 8;
        QByteArray body = QByteArray("WAVEfmt ", 8) + u32(16) + u16(tag) + u16(1) + u32(rate) + u32(rate * block)
                        + u16(block) + u16(bits) + QByteArray("data", 4) + u32(declared) + data + after;
        const QString path = dir.filePath(name);
        QFile f(path);
        if (f.open(QIODevice::WriteOnly))
            f.write(QByteArray("RIFF", 4) + u32(quint32(body.size())) + body);
        return path;
    }

    void wav_donnees_vides_puis_metadonnees_sans_son()
    {
        // bloc « data » vide puis bloc LIST : pas de son (les métadonnées ne sont pas des échantillons)
        QTemporaryDir dir;
        const quint32 listSize = 24;
        const QByteArray list = QByteArray("LIST", 4) + QByteArray(reinterpret_cast<const char *>(&listSize), 4)
                              + QByteArray("INFOINAM\x0f\0\0\0un titre sans son\0", 24);
        const audio::LoadResult r = audio::charge(wav_brut(dir, u"vide.wav"_s, 1, 16, 0, {}, list));
        QVERIFY(!r.audio);
        QCOMPARE(r.error, audio::LoadError::NoAudio);
        // taille 0 suivie d'échantillons (fichier écrit en continu) : tout est lu
        QByteArray samples;
        for (int i = 0; i < 200; ++i) {
            const qint16 v = qint16(i % 2 ? 1000 : -1000);
            samples += QByteArray(reinterpret_cast<const char *>(&v), 2);
        }
        const audio::LoadResult streamed = audio::charge(wav_brut(dir, u"continu.wav"_s, 1, 16, 0, samples));
        QVERIFY(streamed.audio);
        QCOMPARE(streamed.audio->longueur(), size_t(200));
        QVERIFY(!streamed.audio->damaged);
    }

    void wav_plus_court_qu_annonce_endommage()
    {
        // 1 000 échantillons annoncés, 100 présents : enregistrement coupé, signalé
        QTemporaryDir dir;
        const audio::LoadResult r = audio::charge(wav_brut(dir, u"coupe.wav"_s, 1, 16, 2000, QByteArray(200, '\0')));
        QVERIFY(r.audio);
        QCOMPARE(r.audio->longueur(), size_t(100));
        QVERIFY(r.audio->damaged);
        const audio::LoadResult whole = audio::charge(wav_brut(dir, u"entier.wav"_s, 1, 16, 200, QByteArray(200, '\0')));
        QVERIFY(whole.audio && !whole.audio->damaged);
        const auto grand = audio::charge(wav_brut(dir, u"grand-coupe.wav"_s, 1, 16, 0x80000010u, QByteArray(200, '\0')));
        QVERIFY(grand.audio && grand.audio->damaged);
    }

    void wav_en_continu_derniere_trame_incomplete()
    {
        QTemporaryDir dir;
        for (const quint32 taille : {0u, 0x7fffffffu, 0xffffffffu}) {
            // En PCM 16 bits, cinq octets donnent deux échantillons et la moitié du suivant.
            const auto r = audio::charge(wav_brut(dir, u"continu.wav"_s, 1, 16, taille, QByteArray(5, '\0')));
            QVERIFY(r.audio && r.audio->damaged);
            QCOMPARE(r.audio->longueur(), size_t(2));
        }
        // En PCM 24 bits, un échantillon de trois octets peut être suivi d'un octet de bourrage.
        const auto r = audio::charge(wav_brut(dir, u"continu-24.wav"_s, 1, 24, 0, QByteArray(4, '\0')));
        QVERIFY(r.audio && !r.audio->damaged);
        QCOMPARE(r.audio->longueur(), size_t(1));
    }

    void wav_interrompu_en_cours_de_lecture()
    {
        // La taille annoncée reste celle du fichier entier. La coupure se produit
        // seulement quand le lecteur demande les octets, comme sur un support retiré.
        class WavParFragments : public QBuffer {
        public:
            WavParFragments(QByteArray &wav, qint64 fin, bool erreur)
                : QBuffer(&wav), m_fin(fin), m_erreur(erreur) {}
        protected:
            qint64 readData(char *destination, qint64 maximum) override
            {
                if (pos() >= m_fin) {
                    if (m_erreur)
                        setErrorString(u"Support déconnecté"_s);
                    return m_erreur ? -1 : 0;
                }
                // Trois octets par lecture pour une trame stéréo de quatre octets.
                return QBuffer::readData(destination, std::min({maximum, m_fin - pos(), qint64(3)}));
            }
        private:
            qint64 m_fin;
            bool m_erreur;
        };

        QTemporaryDir dir;
        const auto path = dir.filePath(u"long.wav"_s);
        constexpr int cadence = 10;
        const size_t limite = size_t(audio::MAX_SECONDS * cadence);
        QVERIFY(audio::ecrit_wav(path, cadence, {std::vector<float>(limite + 1, 0.25f),
                                                  std::vector<float>(limite + 1, -0.5f)}));
        QFile fichier(path);
        QVERIFY(fichier.open(QIODevice::ReadOnly));
        QByteArray wav = fichier.readAll();
        constexpr qint64 debutSon = 44;
        for (const bool erreur : {false, true}) {
            WavParFragments partiel(wav, debutSon + 2 * 4 + 3, erreur);
            QVERIFY(partiel.open(QIODevice::ReadOnly | QIODevice::Unbuffered));
            const auto r = audio::lit_wav(partiel, path);
            QVERIFY(r && r->audio && r->audio->damaged && !r->audio->truncated);
            QCOMPARE(*r->audio->channels[0], (std::vector<float>{0.25f, 0.25f}));
            QCOMPARE(*r->audio->channels[1], (std::vector<float>{-0.5f, -0.5f}));
        }

        WavParFragments vide(wav, debutSon + 1, false);
        QVERIFY(vide.open(QIODevice::ReadOnly | QIODevice::Unbuffered));
        const auto sansTrame = audio::lit_wav(vide, path);
        QVERIFY(sansTrame && !sansTrame->audio);
        QCOMPARE(sansTrame->error, audio::LoadError::Incomplete);

        WavParFragments retire(wav, debutSon, true);
        QVERIFY(retire.open(QIODevice::ReadOnly | QIODevice::Unbuffered));
        const auto erreur = audio::lit_wav(retire, path);
        QVERIFY(erreur && !erreur->audio);
        QCOMPARE(erreur->error, audio::LoadError::Other);
        QCOMPARE(erreur->message, u"Support déconnecté"_s);

        WavParFragments complet(wav, wav.size(), false);
        QVERIFY(complet.open(QIODevice::ReadOnly | QIODevice::Unbuffered));
        const auto relu = audio::lit_wav(complet, path);
        QVERIFY(relu && relu->audio && !relu->audio->damaged && relu->audio->truncated);
        QCOMPARE(relu->audio->longueur(), limite);
    }

    void wav_echantillons_non_finis_mis_a_zero()
    {
        QTemporaryDir dir;
        QByteArray data;
        for (int i = 0; i < 128; ++i) {
            const float v = std::array{0.0f, 0.5f, std::numeric_limits<float>::quiet_NaN(),
                                       std::numeric_limits<float>::infinity()}[size_t(i % 4)];
            data += QByteArray(reinterpret_cast<const char *>(&v), 4);
        }
        const audio::LoadResult r = audio::charge(wav_brut(dir, u"nan.wav"_s, 3, 32, quint32(data.size()), data));
        QVERIFY(r.audio);
        QVERIFY(r.audio->invalidSamples);
        QCOMPARE(r.audio->crete(), 0.5f);
        QVERIFY(std::ranges::all_of(*r.audio->mix, [](float v) { return std::isfinite(v); }));
    }

    void lit_wav_flottant_et_24_bits()
    {
        // 24 bits et flottant 32 bits : fabriqués par ffmpeg s'il est installé
        QTemporaryDir dir;
        for (const auto &[name, codec] : std::vector<std::pair<QString, QString>>{
                 {u"s24.wav"_s, u"pcm_s24le"_s}, {u"f32.wav"_s, u"pcm_f32le"_s}, {u"u8.wav"_s, u"pcm_u8"_s}}) {
            const QString path = dir.filePath(name);
            const int code = QProcess::execute(u"ffmpeg"_s, {u"-nostdin"_s, u"-v"_s, u"error"_s, u"-y"_s, u"-f"_s,
                                                             u"lavfi"_s, u"-i"_s,
                                                             u"sine=frequency=440:sample_rate=48000:duration=1"_s,
                                                             u"-c:a"_s, codec, path});
            if (code != 0) {
                qWarning("%s : ffmpeg absent, format non essayé", qPrintable(name));
                continue;
            }
            const audio::LoadResult r = audio::charge(path);
            QVERIFY2(r.audio, qPrintable(name));
            QCOMPARE(r.audio->rate, 48000.0);
            QVERIFY2(std::abs(r.audio->duree() - 1.0) < 0.01, qPrintable(name));
            QVERIFY2(std::abs(fondamentale_de(*r.audio, audio::Channel::Mix).value() - 440.0) < 0.1, qPrintable(name));
            // sinusoïde d'amplitude 1/8 (générateur « sine » de ffmpeg)
            QVERIFY2(std::abs(r.audio->crete() - 0.125) < 0.01, qPrintable(name));
        }
    }

    void rejette_fichiers_non_audio()
    {
        QTemporaryDir dir;
        const auto dossier = audio::charge(dir.path());
        QVERIFY(!dossier.audio);
        QCOMPARE(dossier.error, audio::LoadError::Unsupported);
        const QString path = dir.filePath(u"texte.wav"_s);
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("pas un son");
        }
        QVERIFY(!audio::charge(path).audio);
        const audio::LoadResult missing = audio::charge(dir.filePath(u"absent.mp3"_s));
        QVERIFY(!missing.audio);
        QCOMPARE(missing.error, audio::LoadError::Other);
    }

    /// Ouvre chaque format par la même entrée que la fenêtre. Les sons de référence
    /// sont fabriqués sur place avec ffmpeg pour connaître leur durée et leur fréquence.
    void lit_audio_compresse_et_videos()
    {
        QTemporaryDir dir;
        const QStringList video = {u"-f"_s, u"lavfi"_s, u"-i"_s, u"testsrc=size=160x120:rate=25"_s, u"-shortest"_s};
        const std::vector<std::pair<QString, QStringList>> formats = {
            {u"son.mp3"_s, {u"-c:a"_s, u"libmp3lame"_s}},
            {u"son.ogg"_s, {u"-c:a"_s, u"libvorbis"_s}},
            {u"son.flac"_s, {}},
            {u"son.m4a"_s, {u"-c:a"_s, u"aac"_s}},
            {u"son.aac"_s, {u"-c:a"_s, u"aac"_s}},
            {u"son.opus"_s, {u"-c:a"_s, u"libopus"_s}},
            {u"son.aiff"_s, {}},
            {u"video.mp4"_s, video + QStringList{u"-c:v"_s, u"mpeg4"_s, u"-c:a"_s, u"aac"_s}},
            {u"video.avi"_s, video + QStringList{u"-c:v"_s, u"mpeg4"_s, u"-c:a"_s, u"pcm_s16le"_s}},
            {u"video.mkv"_s, video + QStringList{u"-c:v"_s, u"mpeg4"_s, u"-c:a"_s, u"libvorbis"_s}},
        };
        for (const auto &[name, args] : formats) {
            const QString path = dir.filePath(name);
            QStringList all = {u"-nostdin"_s, u"-v"_s, u"error"_s, u"-y"_s, u"-f"_s, u"lavfi"_s, u"-i"_s,
                               u"sine=frequency=440:sample_rate=48000:duration=1"_s};
            all << args << path;
            if (QProcess::execute(u"ffmpeg"_s, all) != 0) {
                qWarning("%s : ffmpeg absent ou sans l'encodeur, format non essayé", qPrintable(name));
                continue;
            }
            const audio::LoadResult r = audio::charge(path);
            QVERIFY2(r.audio, qPrintable(u"%1 : %2 %3"_s.arg(name).arg(int(r.error)).arg(r.message)));
            QCOMPARE(r.audio->rate, 48000.0);
            QVERIFY2(std::abs(r.audio->duree() - 1.0) < 0.1, qPrintable(name));
            const double f0 = fondamentale_de(*r.audio, audio::Channel::Mix).value_or(0.0);
            QVERIFY2(std::abs(f0 - 440.0) < 0.2, qPrintable(u"%1 : %2"_s.arg(name).arg(f0)));
        }
        // vidéo sans piste son
        const QString silent = dir.filePath(u"muette.mp4"_s);
        if (QProcess::execute(u"ffmpeg"_s, QStringList{u"-nostdin"_s, u"-v"_s, u"error"_s, u"-y"_s} + video.mid(0, 4)
                                               + QStringList{u"-t"_s, u"1"_s, u"-c:v"_s, u"mpeg4"_s, silent})
            == 0) {
            const audio::LoadResult r = audio::charge(silent);
            QVERIFY(!r.audio);
            QCOMPARE(r.error, audio::LoadError::NoAudio);
            const auto secours = audio::convertit_avec_ffmpeg(silent, silent);
            QVERIFY(secours && !secours->audio);
            QCOMPARE(secours->error, audio::LoadError::NoAudio);
        }
    }

    void piste_audio_par_defaut()
    {
        if (QStandardPaths::findExecutable(u"ffmpeg"_s).isEmpty())
            QSKIP("ffmpeg est nécessaire pour fabriquer le fichier à deux pistes");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(u"deux-pistes.mkv"_s);
        for (const bool secondeParDefaut : {true, false}) {
            // La deuxième piste est stéréo. Avec une sélection automatique de ffmpeg,
            // elle l'emporterait sur la première même si aucune piste n'est marquée.
            const QStringList args = {u"-nostdin"_s, u"-v"_s, u"error"_s, u"-y"_s,
                u"-f"_s, u"lavfi"_s, u"-i"_s, u"sine=frequency=440:sample_rate=48000:duration=1"_s,
                u"-f"_s, u"lavfi"_s, u"-i"_s, u"sine=frequency=880:sample_rate=48000:duration=1"_s,
                u"-map"_s, u"0:a"_s, u"-map"_s, u"1:a"_s, u"-c:a"_s, u"pcm_s16le"_s, u"-ac:a:1"_s, u"2"_s,
                u"-disposition:a:0"_s, u"0"_s, u"-disposition:a:1"_s, secondeParDefaut ? u"default"_s : u"0"_s, path};
            QCOMPARE(QProcess::execute(u"ffmpeg"_s, args), 0);
            const auto lecture = audio::charge(path);
            const auto secours = audio::convertit_avec_ffmpeg(path, path);
            QVERIFY(lecture.audio && secours && secours->audio);
            const double attendu = secondeParDefaut ? 880 : 440;
            for (const auto *son : {&*lecture.audio, &*secours->audio})
                QVERIFY(std::abs(fondamentale_de(*son, audio::Channel::Mix).value_or(0) - attendu) < 0.2);
        }
    }

    // ------------------------------------------------------------ analyse en fond
    void analyseur_traite_derniere_demande()
    {
        Analyzer analyzer;
        QSignalSpy spy(&analyzer, &Analyzer::mis_a_jour);
        const audio::Audio a = son_test();
        AnalysisKey key{1, audio::Channel::Mix, 0, 22050, {Window::Hann, 0}, PeakParams{}};
        QVERIFY(!analyzer.en_attente());
        analyzer.demande(key, a.mix, a.rate);
        QVERIFY(analyzer.en_attente());
        QTRY_VERIFY(!analyzer.en_attente());
        QVERIFY(analyzer.resultat(1));
        QVERIFY(!analyzer.resultat(2));
        QVERIFY(std::abs(analyzer.resultat(1)->fundamental.value() - 440.0) < 0.1);
        // extrait trop court : pas de résultat, et plus de calcul en attente
        key.last = 4;
        analyzer.demande(key, a.mix, a.rate);
        QVERIFY(!analyzer.resultat(1));
        QTRY_VERIFY(!analyzer.en_attente());
        QVERIFY(!analyzer.resultat(1));
        analyzer.vide();
        QVERIFY(!analyzer.en_attente());
        // un long extrait (une minute, segments de 2¹⁸ échantillons) aussitôt remplacé par un court :
        // seul le dernier compte
        std::vector<float> minute;
        for (int i = 0; i < 20; ++i)
            minute.insert(minute.end(), a.mix->begin(), a.mix->end());
        const auto samples = std::make_shared<const std::vector<float>>(std::move(minute));
        analyzer.demande({2, audio::Channel::Mix, 0, samples->size(), {Window::Hann, 0}, PeakParams{}}, samples, a.rate);
        analyzer.demande({2, audio::Channel::Mix, 0, 4096, {Window::Hann, 0}, PeakParams{}}, samples, a.rate);
        QTRY_VERIFY(!analyzer.en_attente());
        QVERIFY(analyzer.resultat(2));
        QCOMPARE(analyzer.resultat(2)->spectrum.segmentLen, size_t(4096));
    }

    // ------------------------------------------------------------ écoute
    void copie_intervalle_tous_canaux_avec_fondus()
    {
        std::vector<float> samples(10'000);
        for (size_t i = 0; i < samples.size(); ++i)
            samples[i] = (float(i % 100) - 50.0f) / 100.0f;
        Playback p = lecture(samples, 1000.0, 3000, 48000.0, 48000.0);
        std::vector<float> out(2 * 2500);
        p.remplit(out.data(), 2500, 2);
        std::vector<float> left;
        for (size_t i = 0; i < out.size(); i += 2) {
            QCOMPARE(out[i], out[i + 1]);
            left.push_back(out[i]);
        }
        // fondu d'entrée de 5 ms (240 échantillons), puis le signal lui-même, et un fondu avant la fin
        QCOMPARE(left[0], 0.0f);
        for (size_t i = 1; i < 240; ++i)
            QVERIFY(std::abs(left[i]) <= std::abs(samples[1000 + i]));
        for (size_t i = 240; i < 1760; ++i)
            QCOMPARE(left[i], samples[1000 + i]);
        QVERIFY(std::abs(left[1999]) < 0.01);
        for (size_t i = 2000; i < left.size(); ++i)
            QCOMPARE(left[i], 0.0f);
        QVERIFY(p.etat_partage().done);
        QCOMPARE(p.position(), 3000.0);
    }

    void reechantillonne_vers_sortie()
    {
        const auto tone = [](double rate, size_t n) {
            std::vector<float> v(n);
            for (size_t i = 0; i < n; ++i)
                v[i] = float(0.5 * std::sin(2.0 * std::numbers::pi * 1000.0 * double(i) / rate));
            return v;
        };
        const std::vector<float> samples = tone(44100.0, 44100);
        Playback p = lecture(samples, 0.0, samples.size(), 44100.0, 48000.0);
        std::vector<float> out(4800);
        p.remplit(out.data(), 4800, 1);
        const std::vector<float> expected = tone(48000.0, 4800);
        float error = 0.0f;
        for (size_t i = 240; i < out.size(); ++i)
            error = std::max(error, std::abs(out[i] - expected[i]));
        QVERIFY2(error < 0.003f, qPrintable(QString::number(error)));
        QVERIFY(std::abs(p.position() - 4410.0) < 1e-6);
    }

    void ecoute_filtre_les_frequences_hors_bande()
    {
        // La sortie à 16 kHz peut jouer le 1 kHz. Sans filtre, le 10 kHz devient un faux 6 kHz.
        // La seconde cadence impose également des positions intermédiaires entre échantillons.
        for (double cadence : {48000., 44100.}) {
            const auto son = sons_purs({{1000, 0.4}, {10000, 0.5}}, 1, cadence);
            auto lecteur = lecture(son, 0, son.size(), cadence, 16000);
            std::vector<float> sortie(16000);
            lecteur.remplit(sortie.data(), sortie.size(), 1);
            const auto attendu = sons_purs({{1000, 0.4}}, 1, 16000);
            for (size_t i = 100; i < sortie.size() - 100; ++i)
                QVERIFY(std::abs(sortie[i] - attendu[i]) < 0.001);
        }
        // Un extrait silencieux entouré de signal doit rester silencieux, jusque dans les fondus.
        std::vector<float> silence(1200, 0.8f);
        std::fill(silence.begin() + 301, silence.begin() + 901, 0.f);
        auto lecteur = lecture(silence, 301.5, 901, 48000, 16000);
        std::vector<float> sortie(256);
        lecteur.remplit(sortie.data(), sortie.size(), 1);
        QVERIFY(std::ranges::all_of(sortie, [](float x) { return x == 0.f; }));
    }

    void pause_attenue_puis_silence()
    {
        const std::vector<float> samples(48000, 0.5f);
        Playback p = lecture(samples, 0.0, samples.size(), 48000.0, 48000.0);
        std::vector<float> out(1000);
        p.remplit(out.data(), 1000, 1);
        QCOMPARE(out[999], 0.5f);
        p.etat_partage().stop = true;
        p.remplit(out.data(), 1000, 1);
        QVERIFY(out[0] > 0.49f && out[120] < 0.26f && out[120] > 0.24f);
        for (size_t i = 240; i < out.size(); ++i)
            QCOMPARE(out[i], 0.0f);
        QVERIFY(p.etat_partage().done);
        QCOMPARE(p.position(), 1240.0);
    }

    void lecture_pause_reprise_arret()
    {
        const audio::Audio a = son_test();
        const Samples samples = a.signal(audio::Channel::Mix);
        const Clip whole{1, audio::Channel::Mix, 0, a.longueur()};
        Player player(true);
        QVERIFY(!player.position() && !player.en_lecture());
        QVERIFY(!player.joue(whole, samples, a.rate));
        QVERIFY(player.en_lecture() && player.occupe());
        attend(player, [&](const Player &p) { return *p.position() > 0.1 * a.rate; });
        // pause : la position reste fixe, et la lecture reprend là
        player.pause();
        const double paused = player.position().value();
        QVERIFY(player.en_pause() && paused < 0.5 * a.rate);
        attend(player, [](const Player &p) { return !p.occupe(); });
        QCOMPARE(player.position(), paused);
        QVERIFY(!player.joue(whole, samples, a.rate));
        attend(player, [&](const Player &p) { return *p.position() > paused + 0.05 * a.rate; });
        // arrêt : plus de position, la lecture suivante part du début
        player.arrete();
        QVERIFY(!player.position() && !player.extrait_en_lecture());
        QVERIFY(!player.joue(whole, samples, a.rate));
        QVERIFY(*player.position() < 0.05 * a.rate);
        // un autre extrait remplace le premier, et la lecture s'arrête seule à sa fin
        const auto first = size_t(a.rate);
        const Clip extract{1, audio::Channel::Mix, first, first + 4410};
        QVERIFY(!player.joue(extract, samples, a.rate));
        QCOMPARE(*player.extrait_en_lecture(), extract);
        QVERIFY(*player.position() >= a.rate);
        attend(player, [](const Player &p) { return !p.en_lecture(); });
        QVERIFY(!player.position());
    }

    /// Joue une seconde de signal sur la vraie sortie audio, si MEVSONO_CARTE_SON est défini.
    void joue_sur_carte_son()
    {
        if (qEnvironmentVariableIsEmpty("MEVSONO_CARTE_SON"))
            QSKIP("MEVSONO_CARTE_SON : joue un son sur la carte son");
        const audio::Audio a = son_test();
        Player player;
        const Clip clip{1, audio::Channel::Mix, 0, size_t(a.rate)};
        QElapsedTimer clock;
        clock.start();
        const auto error = player.joue(clip, a.mix, a.rate);
        const Tr &tr = textes(Lang::Fr);
        QVERIFY2(!error, qPrintable(error ? tr.audioFailures[size_t(error->failure)] : QString()));
        // position entendue au fil du temps, comparée à l'horloge
        while (player.en_lecture()) {
            QCoreApplication::processEvents();
            if (const auto e = player.met_a_jour())
                QFAIL(qPrintable(tr.audioFailures[size_t(*e)]));
            if (const auto at = player.position())
                qInfo("%6.3f s : position %6.3f s", double(clock.nsecsElapsed()) / 1e9, *at / a.rate);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            QVERIFY(clock.elapsed() < 5000);
        }
    }

    // ------------------------------------------------------------ réglages
    void reglages_aller_retour()
    {
        Settings s;
        s.lang = Lang::En;
        s.theme = Qt::ColorScheme::Dark;
        s.expert = true;
        s.channel = audio::Channel::Right;
        s.window = Window::BlackmanHarris;
        s.fftSize = 16384;
        s.db = true;
        s.logFreq = true;
        s.autoScaleY = false;
        s.fmax = 0.0;
        s.thresholdDb = -55.5;
        s.harmonics = 12;
        s.snapZero = false;
        s.harmonicMarks = true;
        s.uiZoom = 1.2;
        s.lastDir = u"/home/élève/TP = sons"_s;
        s.windowSize = QSize(1400, 900);
        s.maximized = true;
        QCOMPARE(Settings::interprete(s.en_texte()), s);
        QCOMPARE(Settings::interprete(Settings().en_texte()), Settings());
        // thème du bureau et fenêtre à sa taille normale, tant que l'élève n'a rien choisi
        QCOMPARE(Settings().theme, Qt::ColorScheme::Unknown);
        QVERIFY(!Settings().maximized);
        QCOMPARE(Settings::interprete(u"theme = light\n"_s).theme, Qt::ColorScheme::Light);
    }

    void reglages_tolerent_erreurs()
    {
        const Settings s = Settings::interprete(
            u"n'importe quoi\nlanguage = klingon\nwindow = triangle\nfft_size = 1000\nmax_frequency = 3\n"
            u"peak_threshold_db = 5\nharmonics = 999\nui_zoom = 40\nwindow_width = 10\nwindow_height = 600\n  channel = left  \n"_s);
        const Settings d;
        QCOMPARE(s.lang, std::nullopt);
        QCOMPARE(Settings::interprete(u"theme = violet\n"_s).theme, Qt::ColorScheme::Unknown);
        QCOMPARE(s.window, d.window);
        QCOMPARE(s.fftSize, d.fftSize);
        QCOMPARE(s.fmax, d.fmax);
        QCOMPARE(s.thresholdDb, d.thresholdDb);
        QCOMPARE(s.harmonics, 40);
        QCOMPARE(s.uiZoom, 1.0);
        QCOMPARE(s.windowSize, std::nullopt);
        QCOMPARE(s.channel, audio::Channel::Left);
    }

    void fichier_admin_l_emporte()
    {
        QTemporaryDir dir;
        const QString user = dir.filePath(u"user.ini"_s), admin = dir.filePath(u"admin.ini"_s);
        const auto write = [](const QString &path, const QByteArray &text) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(text);
        };
        write(user, "language = en\nmode = expert\nwindow = hamming\nharmonics = 8\n");
        write(admin, "# imposé par le lycée\nlanguage = fr\nmode = simple\n");
        const Settings s = Settings::charge({user, admin});
        QCOMPARE(s.lang, Lang::Fr); // imposés
        QCOMPARE(s.expert, false);
        QCOMPARE(s.window, Window::Hamming); // choix de l'utilisateur conservés
        QCOMPARE(s.harmonics, 8);
        QCOMPARE(Settings::charge({dir.filePath(u"absent.ini"_s), {}}), Settings());

        // Une faute dans le fichier de salle laisse le choix personnel en vigueur.
        write(user, "language = fr\ntheme = dark\nfft_size = 4096\nwindow_width = 1200\nwindow_height = 800\n");
        write(admin, "language = xx\ntheme = mauve\nfft_size = 4294968320\nwindow_width = 1280\n");
        const Settings apresErreur = Settings::charge({user, admin});
        QCOMPARE(apresErreur.windowSize, QSize(1280, 800));
        QCOMPARE(apresErreur.lang, Lang::Fr);
        QCOMPARE(apresErreur.theme, Qt::ColorScheme::Dark);
        QCOMPARE(apresErreur.fftSize, size_t(4096));
        write(admin, "language = auto\ntheme = auto\n");
        const Settings automatique = Settings::charge({user, admin});
        QCOMPARE(automatique.lang, std::nullopt);
        QCOMPARE(automatique.theme, Qt::ColorScheme::Unknown);
    }

    // ------------------------------------------------------------ langues
    void codes_langue()
    {
        QCOMPARE(langue_du_code(u"fr_FR.UTF-8"_s), Lang::Fr);
        QCOMPARE(langue_du_code(u"en-GB"_s), Lang::En);
        QCOMPARE(langue_du_code(u"de_DE"_s), std::nullopt);
        QCOMPARE(langue_du_code(u"C"_s), std::nullopt);
        for (Lang lang : ALL_LANGS)
            QCOMPARE(langue_du_code(code_langue(lang)), lang);
        QCOMPARE(premier_traduit({u"ru"_s, u"it"_s, u"en"_s}), Lang::En);
        QCOMPARE(premier_traduit({}), std::nullopt);
    }

    void nombres_suivent_la_langue()
    {
        QCOMPARE(textes(Lang::En).decimal, u'.');
        QCOMPARE(textes(Lang::Fr).decimal, u',');
        QCOMPARE(textes(Lang::Fr).fftPoints(32768), u"32 768 points"_s);
        QCOMPARE(textes(Lang::En).fftPoints(1048576), u"1,048,576 points"_s);
        QCOMPARE(textes(Lang::En).fftPoints(512), u"512 points"_s);
    }

    // ------------------------------------------------------------ notice
    void ecrit_fichiers_notice_avec_version()
    {
        const QString helpDir = help::dossier();
        QVERIFY(!helpDir.isEmpty());
        QString error;
        const QString page = help::ecrit(Lang::En, &error);
        QCOMPARE(page, QDir(helpDir).filePath(u"en.html"_s));
        QCOMPARE(help::ecrit(Lang::Fr, &error), QDir(helpDir).filePath(u"fr.html"_s));
        for (const QString &name : help::fichiers()) {
            const QString path = QDir(helpDir).filePath(name);
            QVERIFY2(QFileInfo(path).isFile(), qPrintable(name));
            if (name.endsWith(u".png"_s))
                QVERIFY2(!QImage(path).isNull(), qPrintable(name));
        }
        const QString html = lit_texte(page);
        QVERIFY(html.contains(QStringLiteral(MEVSONO_VERSION)) && !html.contains(u"{{version}}"_s));
        QVERIFY(html.contains(u"help.css"_s));
    }

    // Le premier TP doit pouvoir être refait à partir du WAV proposé par la notice hors connexion.
    void mesure_du_la_de_la_notice()
    {
        QString erreur;
        const QString page = help::ecrit(Lang::Fr, &erreur);
        QVERIFY2(!page.isEmpty(), qPrintable(erreur));
        const QString fichier = QFileInfo(page).dir().filePath(u"sinus_440_Hz.wav"_s);
        const auto lecture = audio::charge(fichier);
        QVERIFY(lecture.audio);
        const auto &son = *lecture.audio;
        const auto &signal = *son.mix;
        const double frequenceAttendue = 440.0;
        const double depart = 0.5;
        const double dureeCinqPeriodes = 5.0 / frequenceAttendue;
        const auto premier = passage_par_zero_montant(signal, son.rate, depart, 0.001);
        const auto dernier = passage_par_zero_montant(signal, son.rate, depart + dureeCinqPeriodes, 0.001);
        QVERIFY(premier && dernier);
        const double frequenceMesuree = 5.0 / (*dernier - *premier);
        QVERIFY(std::abs(frequenceMesuree - frequenceAttendue) < 0.1);
        const auto frequenceSpectrale = fondamentale_de(son, audio::Channel::Mix);
        QVERIFY(frequenceSpectrale && std::abs(*frequenceSpectrale - frequenceMesuree) < 0.1);
    }

    /// Chaque image des pages est livrée, chaque image livrée sert, et chaque lien interne mène à une
    /// partie de la page.
    void pages_notice_completes()
    {
        const QStringList files = help::fichiers();
        QStringList used;
        for (Lang langue : ALL_LANGS) {
            const QString page = code_langue(langue) + u".html"_s;
            QString erreur;
            const QString chemin = help::ecrit(langue, &erreur);
            QVERIFY2(!chemin.isEmpty(), qPrintable(erreur));
            const QString html = lit_texte(chemin);
            QVERIFY(!html.isEmpty());
            const auto attributes = [&](const QString &prefix) {
                QStringList out;
                for (qsizetype i = html.indexOf(prefix); i >= 0; i = html.indexOf(prefix, i + 1)) {
                    const qsizetype start = i + prefix.size();
                    out << html.mid(start, html.indexOf(u'"', start) - start);
                }
                return out;
            };
            const QStringList images = attributes(u"<img src=\""_s);
            QVERIFY2(!images.isEmpty(), qPrintable(page));
            const QString suffixe = u"-%1.png"_s.arg(QFileInfo(page).baseName());
            for (const QString &image : images) {
                QVERIFY2(image.endsWith(suffixe), qPrintable(image));
                QVERIFY2(files.contains(image), qPrintable(page + u" : image "_s + image));
                QVERIFY2(html.contains(u"href=\"%1\""_s.arg(image)), qPrintable(image));
                used << image;
            }
            for (const QString &anchor : attributes(u"href=\"#"_s))
                QVERIFY2(html.contains(u"id=\"%1\""_s.arg(anchor)), qPrintable(page + u" : #"_s + anchor));
            for (const QString &source : attributes(u"src=\""_s))
                QVERIFY2(files.contains(source), qPrintable(source));
            // Vérifier pour chaque langue les liens vers les sons réellement fournis avec la notice.
            const auto sons = files.filter(u".wav"_s);
            QCOMPARE(sons.size(), 7);
            for (const QString &son : sons)
                QVERIFY2(html.contains(u"href=\"%1\""_s.arg(son)), qPrintable(son));
            // Autre langue, feuille de style et liens vers les captures en taille réelle.
            for (const QString &other : attributes(u"href=\""_s)) {
                if (!other.startsWith(u'#') && !other.contains(u':'))
                    QVERIFY2(files.contains(other), qPrintable(page + u" : "_s + other));
            }
            // la notice donne les noms de fichiers du programme
            QVERIFY2(html.contains(u"mevsono.ini"_s), qPrintable(page));
        }
        for (const QString &file : files) {
            if (file.endsWith(u".png"_s))
                QVERIFY2(used.contains(file), qPrintable(u"image inutilisée : "_s + file));
        }
    }

    // ------------------------------------------------------------ calculs de l'interface
    void fenetre_la_plus_forte_trouve_son()
    {
        std::vector<float> samples(44100 * 3, 0.0f);
        const std::vector<float> tone = sons_purs({{440.0, 0.5}}, 0.5);
        std::ranges::copy(tone, samples.begin() + 44100);
        const double start = fenetre_la_plus_forte(samples, RATE, 0.5);
        QVERIFY2(std::abs(start - 1.0) < 0.011, qPrintable(QString::number(start)));
        QCOMPARE(fenetre_la_plus_forte(std::span(samples).first(100), RATE, 0.5), 0.0);

        // Le dernier début possible (35 ms) tombe entre deux pas de 10 ms.
        std::vector<float> finSonore(55, 0);
        std::fill(finSonore.end() - 20, finSonore.end(), 1.0f);
        QCOMPARE(fenetre_la_plus_forte(finSonore, 1000, 0.020), 0.035);
    }

    void points_spectre_gardent_pics()
    {
        const std::vector<float> x = sons_purs({{1000.0, 0.5}}, 1.0);
        const auto sp = spectre(x, RATE, {Window::Hann, 0});
        for (bool log : {false, true}) {
            const std::vector<QPointF> pts = points_spectre(*sp, {}, 20.0, 20000.0, 400, log);
            QVERIFY(pts.size() <= 400);
            QPointF top;
            for (const QPointF &p : pts) {
                if (p.y() > top.y())
                    top = p;
            }
            QVERIFY(std::abs(top.x() - 1000.0) < 1.0 && std::abs(top.y() - 0.5) < 0.03);
        }
        QVERIFY(points_spectre(*sp, {}, 30000.0, 40000.0, 400, false).empty());
        QVERIFY(points_spectre(*sp, {}, 20.0, 20000.0, 0, true).empty());
        QVERIFY(points_spectre(*sp, {}, 2000.0, 2000.0, 400, false).empty());
    }

    void points_spectre_atteignent_sommets()
    {
        // son à mi-chemin de deux raies (Δf = 44 100 / 8 192 Hz) : aucune raie n'atteint son amplitude
        const std::vector<float> x = sons_purs({{186.5 * RATE / 8192.0, 0.5}}, 0.1);
        const auto sp = spectre(x, RATE, {Window::Hann, 0});
        QCOMPARE(sp->fftLen, size_t(8192));
        QVERIFY(sp->amplitude_max() < 0.49);
        const std::vector<Peak> found = pics(*sp, Window::Hann, PeakParams{});
        for (size_t columns : {size_t(2000), size_t(50)}) {
            const std::vector<QPointF> pts = points_spectre(*sp, found, 20.0, 5000.0, columns, false);
            QVERIFY(std::ranges::is_sorted(pts, {}, &QPointF::x));
            // la courbe passe par chaque pic affiné, et son sommet est l'amplitude du son
            for (const Peak &p : found)
                QVERIFY(std::ranges::find(pts, QPointF(p.freq, p.amplitude)) != pts.end());
            const double top = std::ranges::max(pts, {}, &QPointF::y).y();
            QVERIFY2(std::abs(top - 0.5) < 0.005, qPrintable(QString::number(top)));
        }
        // Le pic est exactement au bord du zoom, avant la première raie retenue pour le tracé.
        const auto &pic = found.front();
        const auto bord = points_spectre(*sp, found, pic.freq, 5000.0, 20, false);
        QVERIFY(std::ranges::find(bord, QPointF(pic.freq, pic.amplitude)) != bord.end());
    }

    void pic_le_plus_proche_deux_axes()
    {
        const std::vector<Peak> p = {{100.0, 1.0}, {1000.0, 0.5}};
        // 500 Hz : plus près de 100 Hz sur l'axe linéaire, de 1000 Hz sur l'axe logarithmique
        QCOMPARE(pic_le_plus_proche(p, 500.0, false)->freq, 100.0);
        QCOMPARE(pic_le_plus_proche(p, 500.0, true)->freq, 1000.0);
        QVERIFY(!pic_le_plus_proche({}, 500.0, false));
    }

    void graduations_log_rondes()
    {
        QVERIFY(graduations_log(1e50, 2e50).empty());
        QVERIFY(graduations_log(-2e50, -1e50).empty());
        std::vector<double> values;
        for (double m : graduations_log(1.3, 3.7))
            values.push_back(std::round(std::pow(10.0, m)));
        for (double v : {50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0})
            QVERIFY2(std::ranges::find(values, v) != values.end(), qPrintable(QString::number(v)));
    }

    void exports_csv()
    {
        const Tr &tr = textes(Lang::Fr);
        const std::vector<float> x = sons_purs({{440.0, 0.5}}, 0.1);
        const auto sp = spectre(x, RATE, {Window::Hann, 1024});
        const QString dest = qEnvironmentVariable("MEVSUITE_CSV_TEST");
        if (!dest.isEmpty()) {
            for (Lang langue : {Lang::Fr, Lang::En}) {
                QFile f(QDir(dest).filePath(u"spectre-%1.csv"_s.arg(code_langue(langue))));
                QVERIFY(f.open(QIODevice::WriteOnly));
                const auto bytes = csv_spectre(textes(langue), *sp).toUtf8();
                QCOMPARE(f.write(bytes), bytes.size());
            }
        }
        QString csv = csv_spectre(tr, *sp);
        QVERIFY2(csv.startsWith(u"f (Hz);A;A (dB)\n0,0000;"_s), qPrintable(csv.left(40)));
        QCOMPARE(csv.count(u'\n'), 1 + 513);
        QCOMPARE(csv_spectre(tr, *sp, 0, 400) + csv_spectre(tr, *sp, 400, 113), csv);
        csv = csv_extrait(textes(Lang::En), std::vector<float>{0.25f, -0.5f}, 44100, RATE);
        QCOMPARE(csv, u"t (s),signal\n1.000000,0.250000\n1.000023,-0.500000\n"_s);
        const std::vector<Harmonic> h = {{1, 440.0, Peak{440.0, 0.5}}, {2, 880.0, std::nullopt}};
        const QStringList table = texte_tableau_harmoniques(tr, h).split(u'\n');
        QCOMPARE(table[0], u"Rang\tf (Hz)\tf / f₁\tA (dB)\tA / A₁ (%)"_s);
        QCOMPARE(table[1], u"1\t440,00\t1,0000\t-6,02\t100,00"_s);
        QCOMPARE(table[2], u"2\t\t\t\t"_s);
        // fondamentale absente (220 Hz, sans pic) : rapports à sa fréquence, pas d'amplitude relative
        const std::vector<Harmonic> sans = {{1, 220.0, std::nullopt}, {2, 440.0, Peak{440.0, 0.5}}, {3, 660.0, Peak{660.0, 0.25}}};
        const QStringList t2 = texte_tableau_harmoniques(tr, sans).split(u'\n');
        QCOMPARE(t2[1], u"1\t\t\t\t"_s);
        QCOMPARE(t2[2], u"2\t440,00\t2,0000\t-6,02\t"_s);
        QCOMPARE(t2[3], u"3\t660,00\t3,0000\t-12,04\t"_s);
    }

    void extension_csv_ajoutee()
    {
        QTemporaryDir dir;
        QCOMPARE(avec_extension_csv(dir.filePath(u"spectre"_s)), dir.filePath(u"spectre.csv"_s));
        QCOMPARE(avec_extension_csv(dir.filePath(u"spectre.txt"_s)), dir.filePath(u"spectre.txt"_s));
        QFile old(dir.filePath(u"ancien.csv"_s));
        QVERIFY(old.open(QIODevice::WriteOnly));
        old.close();
        // un fichier existant garde son extension (le programme demande avant de le remplacer)
        QCOMPARE(avec_extension_csv(dir.filePath(u"ancien"_s)), dir.filePath(u"ancien.csv"_s));
    }

    void apercu_et_crete_en_opposition_de_phase()
    {
        const auto son = audio::Audio::fabrique(u"stéréo"_s, 8000, {{0.75f, -0.5f, 1.0f}, {-0.75f, 0.5f}}, false);
        // La troisième valeur de la voie gauche est hors de la durée commune.
        QCOMPARE(son.crete(), 0.75f);
        QCOMPARE(*son.mix, (std::vector<float>{0.0f, 0.0f}));
        Session session;
        session.params.expert = true;
        session.params.channel = audio::Channel::Right;
        auto apercu = ApercuSon::prepare(son, session.params);
        const auto enveloppe = apercu.enveloppe;
        session.installe_son(son, std::move(apercu));
        QCOMPARE(&session.loaded->enveloppe(session.canal()), enveloppe.get());
        const auto &voie = *son.signal(audio::Channel::Right);
        QCOMPARE(enveloppe->min_max(voie, 0, voie.size()), std::make_pair(-0.75f, 0.5f));
    }

    void extrait_depend_du_mode()
    {
        // mode simplifié : les 80 % du milieu du son ; mode expert : 0,5 s de la partie la plus forte
        Session s;
        s.installe_son(son_test());
        QVERIFY(std::abs(s.extract[0] - 0.3) < 1e-9 && std::abs(s.extract[1] - 2.7) < 1e-9);
        // on écoute tout le son pour mesurer la période, l'extrait pour étudier les fréquences
        QCOMPARE(s.tool, Tool::Period);
        QCOMPARE(s.choix_ecoute(), Listen::Sound);
        s.tool = Tool::Extract;
        QCOMPARE(s.choix_ecoute(), Listen::Extract);
        QCOMPARE(s.extrait_en_lecture()->first, size_t(std::round(0.3 * RATE)));
        s.params.expert = true;
        s.params.window = Window::FlatTop;
        s.installe_son(son_test());
        QVERIFY(std::abs(s.extract[1] - s.extract[0] - 0.5) < 1e-9);
        QCOMPARE(s.loaded->id, quint64(2));
        QCOMPARE(s.extrait_en_lecture()->first, size_t(0)); // tout le son
        QCOMPARE(s.parametres_spectre().window, Window::FlatTop);
        // en mode simplifié, l'analyse prend les valeurs par défaut
        s.params.expert = false;
        QCOMPARE(s.parametres_spectre().window, Window::Hann);
        QCOMPARE(s.canal(), audio::Channel::Mix);
    }

    void extrait_initial_sur_la_voie_choisie()
    {
        constexpr int cadence = 8000;
        std::vector<float> gauche(4 * cadence), droite(4 * cadence);
        const auto grave = sons_purs({{440, 0.3}}, 1, cadence);
        const auto aigu = sons_purs({{660, 0.9}}, 1, cadence);
        std::ranges::copy(grave, gauche.begin() + 2 * cadence);
        std::ranges::copy(aigu, droite.begin());
        const auto stereo = audio::Audio::fabrique(u"deux micros"_s, cadence, {gauche, droite}, false);
        Session s;
        s.params.expert = true;
        for (const auto canal : {audio::Channel::Left, audio::Channel::Right, audio::Channel::Mix}) {
            s.params.channel = canal;
            s.installe_son(stereo);
            const double debut = canal == audio::Channel::Left ? 2 : 0;
            QVERIFY(s.extract[0] >= debut && s.extract[1] <= debut + 1);
            const auto cle = s.cle_analyse();
            QVERIFY(cle && cle->channel == canal);
            const auto a = analyse(*stereo.signal(canal), cle->first, cle->last, cadence, cle->spectrum, cle->peaks);
            const double frequence = canal == audio::Channel::Left ? 440 : 660;
            QVERIFY(a && a->fundamental && std::abs(*a->fundamental - frequence) < 0.1);
        }
    }
};

QTEST_GUILESS_MAIN(Tests)
#include "tests.moc"
