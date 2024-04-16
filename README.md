# Eko's PNGTuber

A plugin for OBS that provides a dedicated audio source for PNGtubers.

## How to Install

Currently, no full builds are available. They will be provided when development has reached a stable point.

To install, you must build the package with CMake and copy the appropriate .dll and .pdb files into your obs-plugins directory.

## How to Use

Simply create a PNGtuber source and assign the appropriate images, audio source (to control the mouth motion), and settings.

## Recommendations

Most programs which provide similar functionality do so by created a window or web resource which OBS can capture. This can introduce unnecessary lag.

- Provide scaled-down version of your PNGtuber avatars, where applicable, to reduce encoding lag.
- The audio threshhold property on the PNGtuber source does not directly relate to the volume shown in the mixer. Experiment to find what works.
- To avoid having the mouth move in response to background noise, I strongly recommend ensuring that a noise suppression filter (or similar) is applied to your audio source.