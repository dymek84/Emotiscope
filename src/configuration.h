// TODO: Implement configuration.bin update protection
//
// Used to safeguard future firmware updates against reading the wrong pointers
// after changing the order/length of the config struct between firmware updates.
//
// The first official firmware update can have a second depricated typedef called "configuration_type_1"
// which matches the layout of past config structs over time.
// When the configuration.bin file's version number doesn't match on boot, the values in it can be
// ported to the new config struct on boot. The next following save changes the
// version in flash, making this a one-time conversion.
//
// Downgrades are not supported, and the user is warned that the device will be reset to factory settings
#define CONFIGURATION_TYPE ( 1 )

#define CONFIG_FILENAME "/configuration.bin"
#define NOISE_SPECTRUM_FILENAME "/noise_spectrum.bin"
#define NETWORK_CONFIG_FILENAME "/secrets/network.txt"
#define AUDIO_DEBUG_RECORDING_FILENAME "/audio.bin"
#define MIN_SAVE_WAIT_MS (3 * 1000)	 // Values must stabilize for this many seconds to be written to FS

#define MAX_AUDIO_RECORDING_SAMPLES ( 12800 * 3 ) // 3 seconds at [SAMPLE_RATE]

extern lightshow_mode lightshow_modes[];
extern PsychicWebSocketHandler websocket_handler;

volatile bool wifi_config_mode = false;

config configuration = {
	CONFIGURATION_TYPE,

	1.00, // brightness
	0.25, // softness
	0.90, // hue
	0.00, // incandescent_filter
	0.00, // hue_range
	0.75, // speed
	1.00, // saturation
	0.00, // background
	0,    // current_mode
	true, // mirror_mode
	true, // screensaver
	true, // temporal_dithering
};

float noise_spectrum[NUM_FREQS] = {0.0};

int16_t audio_debug_recording[MAX_AUDIO_RECORDING_SAMPLES];
uint32_t audio_recording_index = 0;
volatile bool audio_recording_live = false;

volatile uint32_t last_save_request_ms = 0;
volatile bool save_request_open = false;

volatile bool filesystem_ready = true;

void update_configuration_version(int32_t current_type){
	if(current_type == 1){

	}
}

void sync_configuration_to_client() {
	char config_item_buffer[120];

	websocket_handler.sendAll("clear_config");

	// brightness
	memset(config_item_buffer, 0, 120);
	snprintf(config_item_buffer, 120, "new_config|brightness|float|%.3f", configuration.brightness);
	websocket_handler.sendAll(config_item_buffer);

	// softness
	memset(config_item_buffer, 0, 120);
	snprintf(config_item_buffer, 120, "new_config|softness|float|%.3f", configuration.softness);
	websocket_handler.sendAll(config_item_buffer);

	// speed
	memset(config_item_buffer, 0, 120);
	snprintf(config_item_buffer, 120, "new_config|speed|float|%.3f", configuration.speed);
	websocket_handler.sendAll(config_item_buffer);

	// hue
	memset(config_item_buffer, 0, 120);
	snprintf(config_item_buffer, 120, "new_config|hue|float|%.3f", configuration.hue);
	websocket_handler.sendAll(config_item_buffer);

	// current_mode
	memset(config_item_buffer, 0, 120);
	snprintf(config_item_buffer, 120, "new_config|current_mode|int|%li", configuration.current_mode);
	websocket_handler.sendAll(config_item_buffer);

	// mirror_mode
	memset(config_item_buffer, 0, 120);
	snprintf(config_item_buffer, 120, "new_config|mirror_mode|int|%d", configuration.mirror_mode);
	websocket_handler.sendAll(config_item_buffer);

	// incandescent_filter
	memset(config_item_buffer, 0, 120);
	snprintf(config_item_buffer, 120, "new_config|incandescent|float|%.3f", configuration.incandescent_filter);
	websocket_handler.sendAll(config_item_buffer);

	// hue_range
	memset(config_item_buffer, 0, 120);
	snprintf(config_item_buffer, 120, "new_config|hue_range|float|%.3f", configuration.hue_range);
	websocket_handler.sendAll(config_item_buffer);

	// saturation
	memset(config_item_buffer, 0, 120);
	snprintf(config_item_buffer, 120, "new_config|saturation|float|%.3f", configuration.saturation);
	websocket_handler.sendAll(config_item_buffer);

	// base_coat
	memset(config_item_buffer, 0, 120);
	snprintf(config_item_buffer, 120, "new_config|background|float|%.3f", configuration.background);
	websocket_handler.sendAll(config_item_buffer);

	// screensaver
	memset(config_item_buffer, 0, 120);
	snprintf(config_item_buffer, 120, "new_config|screensaver|int|%li", configuration.screensaver);
	websocket_handler.sendAll(config_item_buffer);

	// temporal_dithering
	memset(config_item_buffer, 0, 120);
	snprintf(config_item_buffer, 120, "new_config|temporal_dithering|int|%li", configuration.temporal_dithering);
	websocket_handler.sendAll(config_item_buffer);

	websocket_handler.sendAll("config_ready");
}

// Save configuration to LittleFS
// TODO: save_config() causes a large frame stutter whenever called
// Maybe I can chunk the file save operations?
bool save_config() {
	File file = LittleFS.open(CONFIG_FILENAME, FILE_WRITE);
	if (!file) {
		printf("Failed to open %s for writing!", CONFIG_FILENAME);
		return false;
	}
	else {
		const uint8_t* ptr = (const uint8_t*)&configuration;

		// Iterate over the size of config and write each byte to the file
		for (size_t i = 0; i < sizeof(config); i++) {
			file.write(ptr[i]);
		}
	}
	file.close();
	return true;
}

// Save noise spectrum to LittleFS
bool save_noise_spectrum() {
	File file = LittleFS.open(NOISE_SPECTRUM_FILENAME, FILE_WRITE);
	if (!file) {
		printf("Failed to open %s for writing!", NOISE_SPECTRUM_FILENAME);
		return false;
	}
	else {
		const uint8_t* ptr = (const uint8_t*)&noise_spectrum;

		// Iterate over the size of noise_spectrum and write each byte to the file
		for (size_t i = 0; i < sizeof(float) * NUM_FREQS; i++) {
			file.write(ptr[i]);
		}
	}
	file.close();
	return true;
}

// Load configuration from LittleFS
bool load_config() {
	// Open the file for reading
	File file = LittleFS.open(CONFIG_FILENAME, FILE_READ);
	if (!file) {
		printf("Failed to open %s for reading!\n", CONFIG_FILENAME);
		return false;
	}
	else {
		// Ensure the configuration structure is sized properly
		if (file.size() != sizeof(config)) {
			printf("Config file size does not match config structure size!\n");
			file.close();
			return false;
		}

		uint8_t* ptr = (uint8_t*)&configuration;  // Pointer to the configuration structure

		// Read the file content into the configuration structure
		for (size_t i = 0; i < sizeof(config); i++) {
			int byte = file.read();	 // Read a byte
			if (byte == -1) {		 // Check for read error or end of file
				printf("Error reading %s!\n", CONFIG_FILENAME);
				break;
			}
			ptr[i] = (uint8_t)byte;	 // Store the byte in the configuration structure
		}
	}
	file.close();  // Close the file after reading
	return true;
}

// Load noise_spectrum from LittleFS
bool load_noise_spectrum() {
	// Open the file for reading
	File file = LittleFS.open(NOISE_SPECTRUM_FILENAME, FILE_READ);
	if (!file) {
		printf("Failed to open %s for reading!\n", NOISE_SPECTRUM_FILENAME);
		return false;
	}
	else {
		// Ensure the noise_spectrum array is sized properly
		if (file.size() != sizeof(float) * NUM_FREQS) {
			printf("Noise spectrum size does not match expected size! (%lu != %lu)\n", file.size(), sizeof(float) * NUM_FREQS);
			file.close();
			return false;
		}

		uint8_t* ptr = (uint8_t*)&noise_spectrum;  // Pointer to the noise_spectrum array

		// Read the file content into the noise_spectrum array
		for (size_t i = 0; i < sizeof(float) * NUM_FREQS; i++) {
			int byte = file.read();	 // Read a byte
			if (byte == -1) {		 // Check for read error or end of file
				printf("Error reading %s!\n", NOISE_SPECTRUM_FILENAME);
				break;
			}
			ptr[i] = (uint8_t)byte;	 // Store the byte in the noise_spectrum array
		}
	}
	file.close();  // Close the file after reading
	return true;
}

void save_config_delayed() {
	last_save_request_ms = t_now_ms;
	save_request_open = true;
}

void sync_configuration_to_file_system() {
	if (save_request_open == true) {
		if ((t_now_ms - last_save_request_ms) >= MIN_SAVE_WAIT_MS) {
			filesystem_ready = false;
			printf("SAVING\n");
			save_config();
			save_noise_spectrum();
			save_request_open = false;
			filesystem_ready = true;
		}
	}
}

// Function to update the network credentials and restart the ESP
void update_network_credentials(String new_ssid, String new_pass) {
    // Check if the file exists and delete it
    if (LittleFS.exists(NETWORK_CONFIG_FILENAME)) {
        LittleFS.remove(NETWORK_CONFIG_FILENAME);
    }

    // Create a new file and open it for writing
    File file = LittleFS.open(NETWORK_CONFIG_FILENAME, "w");
    if (!file) {
        printf("Failed to open %s for writing\n", NETWORK_CONFIG_FILENAME);
        return;
    }

    // Write SSID and Password to the file with a newline character after each
    file.print(new_ssid + "\n");
    file.print(new_pass + "\n");

    // Close the file
    file.close();

    // Print a success message
    printf("Network credentials updated successfully! Restarting to attempt connection\n");

    // Delay for 100 ms
    delay(100);

    // Restart the ESP
    ESP.restart();
}


bool load_network_credentials(){
	memset(wifi_ssid, 0, 64);
	memset(wifi_pass, 0, 64);

	// Open the file for reading
	File file = LittleFS.open(NETWORK_CONFIG_FILENAME, FILE_READ);
	if (!file) {
		printf("Missing network file, failed to open %s for reading!\n", NETWORK_CONFIG_FILENAME);
		return false;
	}
	else {
		if ( file.size() <= 1 ) {
			// It exists, but there's no way this file has proper data
			printf("Invalid network file, failed to open %s for reading!\n", NETWORK_CONFIG_FILENAME);
			return false;
		}

		// Read up to 64 SSID chars
		for(uint8_t i = 0; i < 64; i++){
			if(file.position() < file.size()){
				int byte = file.read();
				if(byte == '\n'){
					break;
				}
				else if(byte == '\r'){
					printf("Skipped stupid ass Windows-style carriage return during parsing! >:(\n");
				}
				else{
					wifi_ssid[i] = byte;
				}
			}
			else{
				printf("Hit end of network file early while parsing!\n");
				return false;
			}
		}
		printf("Parsed SSID: %s\n", wifi_ssid);

		// Read up to 64 pass chars
		for(uint8_t i = 0; i < 64; i++){
			if(file.position() < file.size()){
				int byte = file.read();	 // Read a byte
				if(byte == '\n'){
					break;
				}
				else if(byte == '\r'){
					printf("Skipped stupid ass Windows-style carriage return during parsing! >:(\n");
				}
				else{
					wifi_pass[i] = byte;
				}
			}
			else{
				printf("Hit end of network file early while parsing!\n");
				return false;
			}
		}
		printf("Parsed pass: %s\n", wifi_pass);
	}
	file.close();  // Close the file after reading

	// We fucken did it
	return true;
}

void init_configuration() {
	// Check if wifi config mode was requested
	if (LittleFS.exists("/WIFI_CONFIG_MODE_TRIGGER")) {
        LittleFS.remove("/WIFI_CONFIG_MODE_TRIGGER");
		wifi_config_mode = true;
	}

	// Attempt to load config from flash
	printf("LOADING CONFIG...");
	bool load_success = load_config();

	// If we couldn't load the file, save a fresh copy
	if (load_success == false) {
		printf("FAIL\n");
		save_config();
	}
	else {
		printf("PASS\n");
	}

	// Attempt to load noise_spectrum from flash
	printf("LOADING NOISE SPECTRUM...");
	load_success = load_noise_spectrum();

	// If we couldn't load the file, save a fresh copy
	if (load_success == false) {
		printf("FAIL\n");
		save_noise_spectrum();
	}
	else {
		printf("PASS\n");
	}

	// Attempt to load wifi creds from flash
	printf("LOADING NETWORK...");
	load_success = load_network_credentials();

	// If we couldn't load the file, save a fresh copy
	if (load_success == false) {
		printf("FAIL\n");
	}
	else {
		printf("PASS\n");
	}
}

// Save debug audio recording to LittleFS
bool save_audio_debug_recording() {
	File file = LittleFS.open(AUDIO_DEBUG_RECORDING_FILENAME, FILE_WRITE);
	if (!file) {
		printf("Failed to open %s for writing!", AUDIO_DEBUG_RECORDING_FILENAME);
		return false;
	}
	else {
		const uint8_t* ptr = (const uint8_t*)&audio_debug_recording;

		// Iterate over the size of noise_spectrum and write each byte to the file
		for (size_t i = 0; i < sizeof(int16_t) * MAX_AUDIO_RECORDING_SAMPLES; i++) {
			file.write(ptr[i]);
		}

		printf("Audio debug recording saved!\n");
	}
	file.close();
	return true;
}