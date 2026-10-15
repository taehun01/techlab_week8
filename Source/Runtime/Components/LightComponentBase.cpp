#include "EnginePCH.h"
#include "LightComponentBase.h"

ULightComponentBase::ULightComponentBase()
: Intensity(1.0f), LightColor(255.0f, 255.0f, 255.0f, 255.0f), bVisible(true) 
{}

ULightComponentBase::~ULightComponentBase() {}
