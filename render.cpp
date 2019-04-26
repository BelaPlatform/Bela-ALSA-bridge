#include <vector>
#include <array>
#include <Bela.h> // AuxiliaryTask

//#define DEBUG

#ifdef DEBUG
#include <Gpio.h>
Gpio gpio0;
Gpio gpio1;
Gpio gpio2;
#endif /* DEBUG */

#include <Bela.h>
#include <math.h>
#include <Scope.h>
#include "AlsaIo.h"

AlsaIo alsaIo;
Scope scope;

bool setup(BelaContext* context, void*)
{
	//exit(testCircularBuffer());
#ifdef DEBUG
	gpio0.open(30, OUTPUT);
	gpio1.open(31, OUTPUT);
	gpio2.open(23, OUTPUT);
#endif /* DEBUG */
	unsigned int alsaBlockSize = 128;
	alsaIo.setup("hw:CARD=UAC2Gadget,DEV=0", context->audioInChannels, context->audioOutChannels, alsaBlockSize, alsaBlockSize * 6, context->audioSampleRate);
	scope.setup(3, context->audioSampleRate);
	return true;
}

void render(BelaContext* context, void*)
{
	int numSamples = context->audioInChannels * context->audioFrames;
	alsaIo.sendReceive(context->audioOut, (float*)context->audioIn, numSamples);
	int cpuWaste = 10;
	float out = 0;
	for(unsigned int n = 0; n < context->audioFrames; ++n)
	{
		static float phase;
		phase += 1.f * 2.f * (float)M_PI / context->audioSampleRate;
		if(phase > M_PI)
			phase -= 2.f * (float)M_PI;
		for(unsigned int s = 0; s <  cpuWaste; ++s)
			out += sinf(phase)/cpuWaste;
		scope.log(audioRead(context, n, 0), audioRead(context, n, 1), context->audioOut[n * context->audioOutChannels + 0], context->audioOut[n * context->audioOutChannels + 1]);
	}
	return;
}

void cleanup(BelaContext* context, void*)
{

}
