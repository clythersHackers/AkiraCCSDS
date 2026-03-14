# Advanced Sample Applications

AkiraOS's `AkiraSDK/wasm_apps/` folder contains several advanced sample applications functioning as reference architecture for deeply embedded mathematical interfaces, high-speed input logic, and HID routing. 

## 1. 3D Graphics via Fixed-Point (`imu_3d/main.c`)

This app retrieves accelerometer variables, processes pitch and roll offsets, and renders a 3D bounding box over orthographic geometry. 
**Key Takeaway**: WebAssembly and embedded platforms lack hardware floating-point acceleration. This system utilizes a lightweight `Q14` fixed-point coordinate mapping for matrix transformations.

```c
// 1. Calculate X/Y coordinate ratio map from normalized sensor ratios
int pitch_q14 = asinf_q14(ay_q14);
int roll_q14  = asinf_q14(ax_q14);

// 2. Perform matrix rotation with Q14 Trig Lookup offsets
int m00 = cosf_q14(pitch_q14);
int m20 = sinf_q14(pitch_q14);

// 3. Project to orthogonal display view
int proj_x = (x_rot * SCALE) >> 14;
int proj_y = (y_rot * SCALE) >> 14;

// 4. Draw Depth with Painter's Algorithm overlapping vectors mapped back to front
display_triangle(center_x, center_y, ...);
```

## 2. Macro Pad Keyboard Configurator (`macro_pad/main.c`)

Showcases the power of abstracting Bluetooth LE HID into standardized `akira` bindings over 60Hz polling delays. Validates button edges utilizing hardware bitmasks and immediately translates them into Consumer/Media and standard Keycode streams without blocking.

```c
// Button logic polled continuously inside thread logic at 16.6ms intervals
int edge_mask = buttons_edge();
if (edge_mask) {
    if (edge_mask & (1 << BTN_UP)) {
        // Fire custom OS registry string into target device over BLE
        hid_type_string("akira_run_script\n");
    }
    if (edge_mask & (1 << BTN_LEFT)) {
        // Multimedia / Consumer interface control 
        hid_consumer_send(HID_CONSUMER_PLAY_PAUSE);
    }
    if (edge_mask & (1 << BTN_A)) {
        // Absolute HID state manipulation
        hid_key_press(HID_KEY_A);
        delay(50);
        hid_key_release_all();
    }
}
```

## 3. `logic_analyzer` Interface Rings (`logic_analyzer/main.c`)

A fast GPIO polling engine executing array bit-packing methodologies to construct high-density logic waves on the display subsystem without causing out-of-bounds heap overflows. The interface maps state-tracking via ring buffers.

```c
// Pre-mapping array pointer write-indexes 
#define SAMPLES 272
static uint8_t smp[4][SAMPLES];

// Sample buffer injection with rollover boundary
smp[0][wr] = gpio_read(PIN_0);
smp[1][wr] = gpio_read(PIN_1);

// Redraw logic signals based on previous-state checks avoiding duplicate paint routines
if (prev_state != current_state) {
    display_rect(x, wave_y, 1, h, COLOR_YELLOW);
}
wr = (wr + 1) % SAMPLES; // Advance ring pointer logically
```

Use these examples natively to springboard sophisticated UI or polling logic in your designs.