#pragma once

class QFile;

/// Vérifie que le JPEG ouvert en début de fichier contient la fin de l'image principale.
/// Le décodage des pixels et de l'orientation reste assuré par Qt.
bool jpeg_complet(QFile &fichier);
