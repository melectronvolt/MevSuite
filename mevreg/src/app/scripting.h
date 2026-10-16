// Intégration des résultats des scripts Python : grandeurs créées par `grandeur()`, figures du
// mini-matplotlib devenues des graphes du logiciel.

#pragma once

#include "core/workspace.h"
#include "python/engine.h"

#include <QJsonArray>
#include <QStringList>

#include <utility>
#include <vector>

namespace scripting {

/// Résultat de l'intégration : graphes à ouvrir, fichiers à exporter, messages.
struct Applied {
    std::vector<ViewId> openGraphs;
    std::vector<std::pair<ViewId, QString>> savefig;
    QStringList messages;
    /// Ordres du module `mevreg` pour la fenêtre principale (fenêtres, fichiers, messages).
    QJsonArray actions;
};

Applied applyOutput(Workspace &ws, ViewId script, const python::ScriptOutput &out);
/// Code de départ d'un nouveau script.
QString templateCode(const Workspace &ws);

} // namespace scripting
