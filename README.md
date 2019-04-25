A Bela-to-ALSA bridge.

Pre-requisites:
-  `apt-get install libzita-alsa-pcmi-dev`
- a Bela AuxTaskNonRT with priority > 0
- a 4.14 kernel
- a patched /lib/modules/4.14-.../kernel/drivers/usb/gadget/functino/u_audio.ko: (the patch allow to use fewer than 4 ALSA periods per buffer)
```
diff --git a/drivers/usb/gadget/function/f_uac2.c b/drivers/usb/gadget/function/f_uac2.c
index d063f04..56872e4 100644
--- a/drivers/usb/gadget/function/f_uac2.c
+++ b/drivers/usb/gadget/function/f_uac2.c
@@ -93,10 +93,10 @@ static struct usb_string strings_fn[] = {
        [STR_IO_IT].s = "USBD Out",
        [STR_USB_OT].s = "USBH In",
        [STR_IO_OT].s = "USBD In",
-       [STR_AS_OUT_ALT0].s = "Playback Inactive",
-       [STR_AS_OUT_ALT1].s = "Playback Active",
-       [STR_AS_IN_ALT0].s = "Capture Inactive",
-       [STR_AS_IN_ALT1].s = "Capture Active",
+       [STR_AS_OUT_ALT0].s = "Bela playback (inactive)",
+       [STR_AS_OUT_ALT1].s = "Bela playback (active)",
+       [STR_AS_IN_ALT0].s = "Bela capture (inactive)",
+       [STR_AS_IN_ALT1].s = "Bela capture (active)",
        { },
 };
 
diff --git a/drivers/usb/gadget/function/u_audio.c b/drivers/usb/gadget/function/u_audio.c
index d3a6392..9742fac 100644
--- a/drivers/usb/gadget/function/u_audio.c
+++ b/drivers/usb/gadget/function/u_audio.c
@@ -30,7 +30,7 @@
 
 #define BUFF_SIZE_MAX  (PAGE_SIZE * 16)
 #define PRD_SIZE_MAX   PAGE_SIZE
-#define MIN_PERIODS    4
+#define MIN_PERIODS    1
 
 struct uac_req {
        struct uac_rtd_params *pp; /* parent param */
```

Known issues:
- barely working
- the clock of the ALSA device drifts from the Bela one. This means that underruns or latency increasing over time is possible
- only works when Bela blocksize and ALSA blocksize are the same
- sends inputs to host and receives outputs from host
- ALSA is the master clock here, so you will get occasional dropouts when the clocks drift
- requires rtdm_pruss_irq with `rtdm_event_pulse()` instead of `_signal()`
- when the glitch occurs, ideally the phase of the McASP loop should be reset to 0, however this does not happen. It can be mitigated by running render() at smaller blocksizes and reading and writing from/to the pipe at different blocksizes, keeping track of a reading pointer.

