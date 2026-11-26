/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Text lines to and from the Raspberry Pi over USB CDC.
 *
 * To the Pi, one line per I-Bus frame, as the CM5 NavBridge firmware sent them:
 *   IBUS1 RX <ms since start> <frame in hex>     frame heard on the car I-Bus (LIN1)
 *   IBUS2 RX <ms since start> <frame in hex>     frame heard on the navigation I-Bus (LIN2)
 * e.g. "IBUS1 RX 7323494 8004BF110329". Nothing is queued while the port is closed.
 *
 * And the video source, when the port is opened and whenever it changes, so that the app
 * acts on the buttons only while the Pi's picture is shown:
 *   RGB RPI      Raspberry Pi picture
 *   RGB NAV      factory navigation picture
 * A frame that switches the picture is always reported while "RGB NAV" holds: a switch to
 * the navigation picture is announced before that frame, a switch to the Pi after it.
 *
 * From the Pi, one command per line (case does not matter):
 *   DFU      reply "DFU OK", then hand over to the bootloader, which shows up as a USB DFU
 *            device (0483:5741) until a new firmware is written; the buck stays on
 *   VERSION  reply "VERSION NavBridge-CM4 <version>"
 *   STATUS   reply "STATUS ..." with the power, video and I-Bus state
 *   RGB      reply "RGB RPI" or "RGB NAV"
 *   RGB RPI  show the Pi's picture, reply "RGB RPI"
 *   RGB NAV  show the navigation picture, reply "RGB NAV"
 *   TIME YYYY-MM-DD HH:MM:SS
 *            the Pi's local time, for the car's clock at the next minute (carclock.h):
 *            reply "TIME 2026-11-26 19:46 in 30 s", then "TIME SENT 2026-11-26 19:46"
 *   PDC ON / PDC OFF
 *            ask the PDC for its distances while in reverse, or stop (pdc.h); reply
 *            "PDC ON" or "PDC OFF". Closing the port turns it off.
 * The picture starts on the navigation module at every CM start.
 */
#ifndef HOST_H
#define HOST_H

#include <stdint.h>

void host_poll(void);
void host_report_frame(uint32_t bus, const uint8_t *frame, uint32_t len);
void host_report_rgb(void);  /* "RGB RPI|NAV" if the source changed since the Pi was told */

#endif
