#include "stdafx.h"
#include "logger.h"
#include <fstream>
#include <memory>

std::unique_ptr<std::wostream> stream;
std::unique_ptr<std::wostream> spatialStream;

void logger::details::log(const std::wstring & message)
{
	if (!stream)
	{
		stream = std::unique_ptr<std::wostream>(new std::wofstream("log.txt"));
	}

	*stream << message << std::endl << std::flush;
	stream->flush();
}

void logger::logSpatialGains(float azDeg, float elDeg, float effectiveMultiplier, const float* snap, const float* peak, int numGains)
{
	if (!spatialStream)
		spatialStream = std::unique_ptr<std::wostream>(new std::wofstream("spatial_debug.txt"));

	std::wstringstream ss;
	ss << L"az=" << azDeg << L" el=" << elDeg << L" mult=" << effectiveMultiplier << L" |";
	for (int i = 0; i < numGains; ++i)
		ss << L" d" << i << L"=" << snap[i] << L"(pk:" << peak[i] << L")";
	*spatialStream << ss.str() << L"\n";
	spatialStream->flush();
}
