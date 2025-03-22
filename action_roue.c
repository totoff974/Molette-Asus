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
#include <stdbool.h>
#include <poll.h> 

#define CONFIG_FILE_NAME "params.cfg"
#define SYS_HIDRAW_PATH "/sys/class/hidraw"
#define BUFFER_SIZE 8

#define DEVICE_HID_ID "0018:00000B05:00000220"
#define DEVICE_HID_NAME "ASUS2020:00 0B05:0220"
#define DEVICE_MODALIAS "hid:b0018g0001v00000B05p00000220"

bool Debug = false;

#define MAX_KEYS 5
#define MAX_BINDINGS 10

typedef struct {
    char action[50];
    char keys[MAX_KEYS][20];  // Stocker jusqu'à MAX_KEYS touches sous forme de texte
    int key_count;  // Nombre de touches associées à l'action
} KeyBinding;

KeyBinding bindings[MAX_BINDINGS];
int binding_count = 0;

int fdvirt;

char config_file_path[512];

unsigned short get_keycode(const char *key_name) {
    // Mappe les noms de touches vers les codes associés
    if (strcmp(key_name, "SUPER") == 0) return KEY_LEFTMETA;
    if (strcmp(key_name, "CTRL") == 0) return KEY_LEFTCTRL;
    if (strcmp(key_name, "ALT") == 0) return KEY_LEFTALT;
    if (strcmp(key_name, "TAB") == 0) return KEY_TAB;
    if (strcmp(key_name, "ENTER") == 0) return KEY_ENTER;
    if (strcmp(key_name, "PAGEDOWN") == 0) return KEY_PAGEDOWN;
    if (strcmp(key_name, "PAGEUP") == 0) return KEY_PAGEUP;
    if (strcmp(key_name, ")") == 0) return KEY_RIGHTBRACE;
    if (strcmp(key_name, "()") == 0) return KEY_LEFTBRACE;
    if (strcmp(key_name, "=") == 0) return KEY_EQUAL;

    if (strlen(key_name) == 1 && isalpha(key_name[0])) {
        return KEY_A + (toupper(key_name[0]) - 'A'); 
    }
    return 0;
}

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

        char action[50];
        char keys[MAX_KEYS][20];  // Tableau temporaire pour stocker les touches
        int num_keys = 0;

        char *token = strtok(line, "=");
        if (token) {
            strcpy(action, token);
            token = strtok(NULL, "+\n");
            while (token && num_keys < MAX_KEYS) {
                strcpy(keys[num_keys], token);
                token = strtok(NULL, "+\n");
                num_keys++;
            }
        }

        if (num_keys > 0) {
            strcpy(bindings[binding_count].action, action);
            bindings[binding_count].key_count = num_keys;
            for (int i = 0; i < num_keys; i++) {
                strcpy(bindings[binding_count].keys[i], keys[i]);
            }
            binding_count++;

            if (Debug) {
                printf("Chargé: %s ->", action);
                for (int i = 0; i < num_keys; i++) {
                    printf(" %s", keys[i]);
                }
                printf("\n");
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

int setup_uinput_device() {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("Erreur d'ouverture de /dev/uinput");
        return -1;
    }

    // Activer les événements de type touche
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    for (int i = 0; i < 256; i++) {  // Active toutes les touches possibles
        ioctl(fd, UI_SET_KEYBIT, i);
    }

    struct uinput_setup usetup;
    memset(&usetup, 0, sizeof(usetup));
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor = 0x1234;  // ID fictif
    usetup.id.product = 0x5678;
    strcpy(usetup.name, "Fake Keyboard");

    if (ioctl(fd, UI_DEV_SETUP, &usetup) < 0) {
        perror("Erreur lors de UI_DEV_SETUP");
        close(fd);
        return -1;
    }

    if (ioctl(fd, UI_DEV_CREATE) < 0) {
        perror("Erreur lors de UI_DEV_CREATE");
        close(fd);
        return -1;
    }

    usleep(100000);  // Petit délai pour s'assurer que le périphérique est prêt

    return fd;  // Retourne le descripteur de fichier ouvert
}


// Fonction pour simuler une combinaison de touches via uinput
void send_key_combination(int fd, unsigned short *keycodes, size_t num_keys) {
    if (fd < 0) {
        fprintf(stderr, "Erreur : uinput non initialisé\n");
        return;
    }
    if (Debug) {
        printf("Envoi de %zu touches : ", num_keys);
    }
    for (size_t i = 0; i < num_keys; i++) {
        if (Debug) {
            printf("%d ", keycodes[i]);
        }
        send_event(fd, EV_KEY, keycodes[i], 1);  // Appuie
    }
    if (Debug) {
        printf("\n");
    }

    send_event(fd, EV_SYN, SYN_REPORT, 0);
    usleep(50000);  // Petit délai pour simuler un appui court

    for (size_t i = 0; i < num_keys; i++) {
        send_event(fd, EV_KEY, keycodes[i], 0);  // Relâche
    }
    send_event(fd, EV_SYN, SYN_REPORT, 0);
}

void execute_action(int fd, const char *action) {
    for (int i = 0; i < binding_count; i++) {
        if (strcmp(bindings[i].action, action) == 0) {
            if (Debug) {
                printf("Exécution de l'action : %s\n", action);
            }

            unsigned short keycodes[MAX_KEYS];
            size_t num_keys = bindings[i].key_count;

            for (size_t j = 0; j < num_keys; j++) {
                keycodes[j] = get_keycode(bindings[i].keys[j]);
                if (Debug) {
                    printf("  -> Clé : %s (code: %d)\n", bindings[i].keys[j], keycodes[j]);
                }
            }

            send_key_combination(fd, keycodes, num_keys);
            return;
        }
    }
    if (Debug) {
        printf("Action inconnue : %s\n", action);
    }
}

void read_hidraw_data(int fdvirt, const char *device_path) {
    int fd = open(device_path, O_RDONLY);
    if (fd < 0) {
        perror("Erreur ouverture HID");
        return;
    }

    struct pollfd fds;
    fds.fd = fd;
    fds.events = POLLIN;  // On surveille les données en entrée

    uint8_t data[BUFFER_SIZE];
    uint8_t last_data[BUFFER_SIZE] = {0}; // Stocker les données précédentes

    while (1) {
        int ret = poll(&fds, 1, -1);  // Attente infinie d'un événement
        if (ret < 0) {
            perror("Erreur poll()");
            break;
        }

        if (fds.revents & POLLIN) {  // Si on a reçu des données
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
                    execute_action(fdvirt, "ACTION_TOURNER_DROITE");
                    if (Debug) {
                        printf("ACTION_ROUE_DROITE\n");
                    }
                }
                else if (memcmp(&data[1], (uint8_t[]){0x00, 0xFF, 0xFF}, 3) == 0) {
                    execute_action(fdvirt, "ACTION_TOURNER_GAUCHE");
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
                        execute_action(fdvirt, "ACTION_CLIC_ROUE");
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
                        execute_action(fdvirt, "ACTION_APPUYER_ROUE_DR");
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
                        execute_action(fdvirt, "ACTION_APPUYER_ROUE_GA");
                    }
                }
                else {
                    if (Debug) {
                        printf("Action inconnue, aucune action effectuée.\n");
                    }
                }
            }
        }
    }

    close(fd);
}

void close_uinput_device() {
    ioctl(fdvirt, UI_DEV_DESTROY);
    close(fdvirt);
    if (Debug) {
        printf("Périphérique uinput fermé proprement.\n");
    }
}

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "--debug") == 0) Debug = true;

    set_config_path();
    load_config();

    fdvirt = setup_uinput_device();
    if (fdvirt < 0) {
        fprintf(stderr, "Impossible d'initialiser uinput\n");
        return 1;
    }

    char *device_path = find_hidraw_device();
    if (!device_path) {
        if (Debug) {
            printf("Aucun périphérique HID trouvé.\n");
        }
        return EXIT_FAILURE;
    }
    read_hidraw_data(fdvirt, device_path);

    atexit(close_uinput_device);

    return EXIT_SUCCESS;
}

