#include "session.h"

using namespace Qt::StringLiterals;

std::vector<std::pair<Target, Segment>> Session::editable() const
{
    std::vector<std::pair<Target, Segment>> out;
    switch (tool) {
    case Tool::Calibrate:
        if (calibSeg)
            out.push_back({Target{Target::Calib}, *calibSeg});
        break;
    case Tool::Measure:
        for (size_t i = 0; i < measures.size(); ++i)
            out.push_back({Target{Target::Measure, static_cast<int>(i)}, measures[i]});
        break;
    case Tool::Profile:
        if (cut)
            out.push_back({Target{Target::Cut}, *cut});
        break;
    }
    return out;
}

Segment *Session::segment(Target target)
{
    switch (target.kind) {
    case Target::Calib: return calibSeg ? &*calibSeg : nullptr;
    case Target::Measure:
        return target.index >= 0 && size_t(target.index) < measures.size() ? &measures[target.index] : nullptr;
    case Target::Cut: return cut ? &*cut : nullptr;
    }
    return nullptr;
}

std::optional<Calibration> Session::calibFromInputs() const
{
    const auto value = parseNum(calibInput);
    if (!value || !calibSeg)
        return std::nullopt;
    return Calibration::make(calibSeg->lenPx(), *value, calibUnit);
}

bool Session::calibPending() const
{
    if (!calibration)
        return false;
    const auto inputs = calibFromInputs();
    return !inputs || !inputs->matches(*calibration);
}

void Session::setImage(std::shared_ptr<const LoadedImage> img)
{
    image = std::move(img);
    tool = Tool::Calibrate;
    calibSeg.reset();
    calibration.reset();
    measures.clear();
    cut.reset();
    m_cached.reset();
}

std::pair<QString, double> Session::unitScale() const
{
    if (calibration)
        return {unitSymbol(calibration->unit), calibration->unitsPerPx()};
    return {u"px"_s, 1.0};
}

const Profile *Session::profile()
{
    if (!image || !cut) {
        m_cached.reset();
        return nullptr;
    }
    if (!m_cached || m_cached->seg != *cut || m_cached->band != band || m_cached->channel != channel)
        m_cached = CachedProfile{*cut, band, channel, sampleProfile(image->rgb, *cut, band, channel)};
    return &m_cached->profile;
}
