#pragma once

#include "session.h"

#include <QJsonObject>

/// En-tête JSON de la séance. Les extrémités des segments sont en pixels de la photo.
QByteArray donnees_seance(const ContenuSeance &session);
/// Valide les données avant de remplacer le travail en mémoire. La photo doit déjà être chargée.
bool restaure_seance(Seance &session, const QJsonObject &donnees);
