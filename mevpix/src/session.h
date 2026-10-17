// État du travail sur l'image : outil, étalon, mesures, ligne de coupe. Partagé par la fenêtre
// principale, la zone image et le graphique du profil.

#pragma once

#include "model.h"
#include "profile.h"
#include "view.h"

#include <QString>

#include <array>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

enum class Tool { Calibrate, Measure, Profile };

/// Segment modifiable à la souris.
struct Target {
    enum Kind { Calib, Measure, Cut } kind;
    /// Numéro de la mesure (`Measure`).
    int index = 0;
};

struct Session {
    std::shared_ptr<const LoadedImage> image;
    Tool tool = Tool::Calibrate;

    std::optional<Segment> calibSeg;
    QString calibInput;
    Unit calibUnit = Unit::Millimetre;
    std::optional<Calibration> calibration;

    std::vector<Segment> measures;

    std::optional<Segment> cut;
    int band = 5;
    Channel channel = Channel::Luminance;
    /// Position des curseurs A et B, en fraction de la longueur de la coupe.
    std::array<double, 2> cursorT = {0.25, 0.75};

    /// Segments modifiables avec l'outil actuel.
    std::vector<std::pair<Target, Segment>> editable() const;
    Segment *segment(Target target);
    /// Étalonnage que donneraient le segment, la longueur et l'unité actuellement saisis.
    std::optional<Calibration> calibFromInputs() const;
    /// Vrai si l'étalon, la longueur ou l'unité ont changé depuis la validation.
    bool calibPending() const;
    /// Vrai si changer d'image ferait perdre un étalonnage, des mesures ou une coupe.
    bool hasWork() const { return calibration.has_value() || !measures.empty() || cut.has_value(); }
    /// Une nouvelle image invalide l'étalonnage et les tracés précédents.
    void setImage(std::shared_ptr<const LoadedImage> img);

    /// Unité des abscisses du profil et sa valeur par pixel.
    std::pair<QString, double> unitScale() const;
    /// Profil de la coupe actuelle (nul sans coupe), recalculé seulement si la coupe, l'épaisseur ou
    /// le canal ont changé.
    const Profile *profile();

private:
    struct CachedProfile {
        Segment seg;
        int band;
        Channel channel;
        Profile profile;
    };
    std::optional<CachedProfile> m_cached;
};
