// Original work, MIT licensed.

#include "Core/KCFDeveloperSettings.h"

UKCFDeveloperSettings::UKCFDeveloperSettings()
{
	CategoryName = TEXT("Game");
	SectionName = TEXT("Kart Combat Framework");
}

const UKCFDeveloperSettings* UKCFDeveloperSettings::Get()
{
	// GetDefault<> on a UDeveloperSettings never returns null once the CDO exists, which it does
	// from module load onward. Callers therefore do not need a null check.
	return GetDefault<UKCFDeveloperSettings>();
}

FName UKCFDeveloperSettings::GetCategoryName() const
{
	return TEXT("Game");
}
