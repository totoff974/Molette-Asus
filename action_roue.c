#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <linux/uinput.h>
#include <libgen.h>
#include <ctype.h>
#include <stdbool.h>  // Inclure pour `bool`

#define CONFIG_FILE_NAME "params.cfg"
#define SYS_HIDRAW_PATH "/sys/class/hidraw"
#define BUFFER_SIZE 8

#define DEVICE_HID_ID "0018:00000B05:00000220"
#define DEVICE_HID_NAME "ASUS2020:00 0B05:0220"
#define DEVICE_MODALIAS "hid:b0018g0001v00000B05p00000220"

// Ajout de la variable Debug
bool Debug = false;  // Mettre à `false` pour désactiver les logs

// Table de conversion des noms de touches vers leurs codes
unsigned short get_keycode(const char *key_name) {
    // Mappe les noms de touches vers les codes associés
    if (strcmp(key_name, "SUPER") == 0) return KEY_LEFTMETA;
    if (strcmp(key_name, "ENTER") == 0) return KEY_ENTER;
    if (strcmp(key_name, "PAGEDOWN") == 0) return KEY_PAGEDOWN;
    if (strcmp(key_name, "PAGEUP") == 0) return KEY_PAGEUP;
    if (strcmp(key_name, ")") == 0) return KEY_RIGHTBRACE;
    if (strcmp(key_name, "=") == 0) return KEY_EQUAL;
    if (strcmp(key_name, "A") == 0) return KEY_A;
    if (strcmp(key_name, "B") == 0) return KEY_B;
    if (strcmp(key_name, "C") == 0) return KEY_C;
    if (strcmp(key_name, "D") == 0) return KEY_D;
    if (strcmp(key_name, "F") == 0) return KEY_F;
    if (strcmp(key_name, "G") == 0) return KEY_G;
    if (strcmp(key_name, "H") == 0) return KEY_H;
    if (strcmp(key_name, "I") == 0) return KEY_I;
    if (strcmp(key_name, "J") == 0) return KEY_J;
    if (strcmp(key_name, "K") == 0) return KEY_K;
    if (strcmp(key_name, "L") == 0) return KEY_L;
    return 0;
}

typedef struct {
    char action[50];
    unsigned short key1;
    unsigned short key2;
} KeyBinding;

KeyBinding bindings[10];
int binding_count = 0;

char config_file_path[512];

void set_config_path() {
    char exe_path[512];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len != -1) {
        exe_path[len] = '\0';
        char *dir = dirname(exe_path);
        snprintf(config_file_path, sizeof(config_file_path), "%s/%s", dir, CONFIG_FILE_NAME);
    } else {
        perror("Impossible de récupérer le chemin de l'exécutable");
        exit(EXIT_FAILURE);
    }
}

int is_target_device(const char *hidraw) {
    char uevent_path[512];
    snprintf(uevent_path, sizeof(uevent_path), "%s/%s/device/uevent", SYS_HIDRAW_PATH, hidraw);
    FILE *file = fopen(uevent_path, "r");
    if (!file) return 0;

    char line[256];
    int match = 0;
    while (fgets(line, sizeof(line), file)) {
        if (strstr(line, DEVICE_HID_ID) || strstr(line, DEVICE_HID_NAME) || strstr(line, DEVICE_MODALIAS)) {
            match++;
        }
    }
    fclose(file);
    return (match >= 1);
}

char *find_hidraw_device() {
    static char device_path[64] = "";
    struct dirent *entry;
    DIR *dir = opendir(SYS_HIDRAW_PATH);
    if (!dir) {
        perror("Erreur ouverture hidraw");
        return NULL;
    }
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.') continue;
        if (is_target_device(entry->d_name)) {
            snprintf(device_path, sizeof(device_path), "/dev/%s", entry->d_name);
            closedir(dir);
            return device_path;
        }
    }
    closedir(dir);
    return NULL;
}

void load_config() {
    FILE *file = fopen(config_file_path, "r");
    if (!file) {
        perror("Impossible d'ouvrir params.cfg");
        return;
    }

    char line[128];
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '#' || strlen(line) < 3) continue;
        char action[50], key1[20], key2[20] = "";
        if (sscanf(line, "%49[^=]=%19[^+]+%19s", action, key1, key2) >= 2) {
            bindings[binding_count].key1 = get_keycode(key1);
            bindings[binding_count].key2 = (strlen(key2) > 0) ? get_keycode(key2) : 0;
            strcpy(bindings[binding_count].action, action);
            binding_count++;

            if (Debug) {
                printf("Chargé: %s -> %s + %s\n", action, key1, key2);
            }
        }
    }
    fclose(file);
}

void send_event(int fd, unsigned short type, unsigned short code, int value) {
    struct input_event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.code = code;
    event.value = value;
    write(fd, &event, sizeof(event));
}

// Fonction pour simuler une combinaison de touches via uinput
void send_key_combination(int fd, unsigned short *keycodes, size_t num_keys) {
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    for (size_t i = 0; i < num_keys; i++) {
        ioctl(fd, UI_SET_KEYBIT, keycodes[i]);
    }

    struct uinput_setup usetup;
    memset(&usetup, 0, sizeof(usetup));
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor = 0x1234;
    usetup.id.product = 0x5678;
    strcpy(usetup.name, "Fake Keyboard");

    ioctl(fd, UI_DEV_SETUP, &usetup);
    ioctl(fd, UI_DEV_CREATE);

    usleep(100000);

    for (size_t i = 0; i < num_keys; i++) {
        send_event(fd, EV_KEY, keycodes[i], 1);
    }
    send_event(fd, EV_SYN, SYN_REPORT, 0);

    for (size_t i = 0; i < num_keys; i++) {
        send_event(fd, EV_KEY, keycodes[i], 0);
    }
    send_event(fd, EV_SYN, SYN_REPORT, 0);

    ioctl(fd, UI_DEV_DESTROY);
}

void execute_action(const char *action) {
    for (int i = 0; i < binding_count; i++) {
        if (strcmp(bindings[i].action, action) == 0) {
            unsigned short keys[] = {bindings[i].key1, bindings[i].key2};
            size_t num_keys = (bindings[i].key2 != 0) ? 2 : 1;
            int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
            if (fd < 0) {
                perror("Erreur d'ouverture uinput");
                return;
            }
            send_key_combination(fd, keys, num_keys);
            close(fd);
            return;
        }
    }
    if (Debug) {
        printf("Action inconnue : %s\n", action);
    }
}

void read_hidraw_data(const char *device_path) {
    int fd = open(device_path, O_RDONLY);
    if (fd < 0) {
        perror("Erreur ouverture HID");
        return;
    }

    uint8_t data[BUFFER_SIZE];
    uint8_t last_data[BUFFER_SIZE] = {0}; // Stocker les données précédentes

    while (1) {
        ssize_t bytesRead = read(fd, data, BUFFER_SIZE);
        if (bytesRead < 0) {
            perror("Erreur de lecture du périphérique HID");
            break;  // Sortie de la boucle en cas d'erreur
        }

        if (bytesRead >= 4) {  // Vérification pour éviter un dépassement de tampon
            // Affichage des données brutes en hexadécimal
            if (Debug) {
                printf("Données HID : ");
                for (int i = 0; i < bytesRead; i++) {
                    printf("%02X ", data[i]);
                }
                printf("\n");
            }

            // Comparaison des données pour déterminer l'action
            if (memcmp(&data[1], (uint8_t[]){0x00, 0x01, 0x00}, 3) == 0) {
                execute_action("ACTION_TOURNER_DROITE");
                if (Debug) {
                    printf("ACTION_ROUE_DROITE\n");
                }
            }
            else if (memcmp(&data[1], (uint8_t[]){0x00, 0xFF, 0xFF}, 3) == 0) {
                execute_action("ACTION_TOURNER_GAUCHE");
                if (Debug) {
                    printf("ACTION_ROUE_GAUCHE\n");
                }
            }

            // Détection des clics
            else if (memcmp(&data[1], (uint8_t[]){0x01, 0x00, 0x00}, 3) == 0) {
                if (Debug) {
                    printf("Clic... En attente de l'action suivante...\n");
                }
                memcpy(last_data, data, BUFFER_SIZE);  // Stocker l'état du clic
            }
            else if (memcmp(&data[1], (uint8_t[]){0x00, 0x00, 0x00}, 3) == 0) {
                if (memcmp(&last_data[1], (uint8_t[]){0x01, 0x00, 0x00}, 3) == 0) {
                    memset(last_data, 0, BUFFER_SIZE);
                    if (Debug) {
                        printf("Relâchement après clic, prise en compte.\n");
                    }
                    execute_action("ACTION_CLIC_ROUE");
                }
            }

            // Détection clic + tourne à droite
            else if (memcmp(&data[1], (uint8_t[]){0x01, 0x01, 0x00}, 3) == 0) {
                if (memcmp(&last_data[1], (uint8_t[]){0x01, 0x00, 0x00}, 3) == 0 ||
                    memcmp(&last_data[1], (uint8_t[]){0x01, 0x01, 0x00}, 3) == 0 ||
                    memcmp(&last_data[1], (uint8_t[]){0x01, 0xFF, 0xFF}, 3) == 0) {
                    memcpy(last_data, data, BUFFER_SIZE);
                    if (Debug) {
                        printf("Clic + tourne à droite.\n");
                    }
                    execute_action("ACTION_APPUYER_ROUE_DR");
                }
            }

            // Détection clic + tourne à gauche
            else if (memcmp(&data[1], (uint8_t[]){0x01, 0xFF, 0xFF}, 3) == 0) {
                if (memcmp(&last_data[1], (uint8_t[]){0x01, 0x00, 0x00}, 3) == 0 ||
                    memcmp(&last_data[1], (uint8_t[]){0x01, 0x01, 0x00}, 3) == 0 ||
                    memcmp(&last_data[1], (uint8_t[]){0x01, 0xFF, 0xFF}, 3) == 0) {
                    memcpy(last_data, data, BUFFER_SIZE);
                    if (Debug) {
                        printf("Clic + tourne à gauche.\n");
                    }
                    execute_action("ACTION_APPUYER_ROUE_GA");
                }
            }
            else {
                if (Debug) {
                    printf("Action inconnue, aucune action effectuée.\n");
                }
            }
        }
    }

    close(fd);
}

int main() {
    set_config_path();
    load_config();
    char *device_path = find_hidraw_device();
    if (!device_path) {
        printf("Aucun périphérique HID trouvé.\n");
        return EXIT_FAILURE;
    }
    read_hidraw_data(device_path);
    return EXIT_SUCCESS;
}

