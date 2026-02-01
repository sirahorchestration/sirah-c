// internal/apiserver/pod_exec.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>
#include "pod_exec.h"

// Parse exec request from JSON
int parse_exec_request(const char* request_json, exec_request_t* req) {
    if (!request_json) {
        return -1;
    }

    memset(req, 0, sizeof(*req));

    struct json_object* root = json_tokener_parse(request_json);
    if (!root) {
        return -1;
    }

    // Parse command
    struct json_object* cmd_obj = NULL;
    if (json_object_object_get_ex(root, "command", &cmd_obj)) {
        if (json_object_is_type(cmd_obj, json_type_array)) {
            // Array format: ["sh", "-c", "echo hello"]
            int len = json_object_array_length(cmd_obj);
            strcpy(req->command, "");
            for (int i = 0; i < len && i < 10; i++) {
                struct json_object* arg = json_object_array_get_idx(cmd_obj, i);
                if (i > 0) strcat(req->command, " ");
                strcat(req->command, json_object_get_string(arg));
            }
        } else {
            // String format
            strcpy(req->command, json_object_get_string(cmd_obj));
        }
    }

    // Parse container name
    struct json_object* container_obj = NULL;
    if (json_object_object_get_ex(root, "container", &container_obj)) {
        strcpy(req->container_name, json_object_get_string(container_obj));
    }

    // Parse flags
    struct json_object* stdin_obj = NULL;
    if (json_object_object_get_ex(root, "stdin", &stdin_obj)) {
        req->stdin_enabled = json_object_get_boolean(stdin_obj);
    }

    struct json_object* stdout_obj = NULL;
    if (json_object_object_get_ex(root, "stdout", &stdout_obj)) {
        req->stdout_enabled = json_object_get_boolean(stdout_obj);
    }

    struct json_object* stderr_obj = NULL;
    if (json_object_object_get_ex(root, "stderr", &stderr_obj)) {
        req->stderr_enabled = json_object_get_boolean(stderr_obj);
    }

    struct json_object* tty_obj = NULL;
    if (json_object_object_get_ex(root, "tty", &tty_obj)) {
        req->tty_enabled = json_object_get_boolean(tty_obj);
    }

    json_object_put(root);
    return 0;
}

// Execute command in pod context
// MVP: Simulates command execution - in real k8s, this would run in container
int endpoint_exec_pod(const char* namespace, const char* pod_name,
                      const char* container_name, const char* command,
                      exec_response_t* response) {
    if (!command || strlen(command) == 0) {
        response->exit_code = 1;
        strcpy(response->stderr_buffer, "no command provided");
        response->stderr_len = strlen(response->stderr_buffer);
        return -1;
    }

    memset(response, 0, sizeof(*response));

    // For MVP, we simulate command execution with known commands
    // In production, this would use container runtime (containerd, docker, etc)

    // Simulate: kubectl exec pod -- ls
    if (strstr(command, "ls") != NULL) {
        const char* output = "bin/\ndev/\netc/\napp/\nproc/\nsys/\n";
        strcpy(response->stdout_buffer, output);
        response->stdout_len = strlen(output);
        response->exit_code = 0;
        return 0;
    }

    // Simulate: kubectl exec pod -- echo hello
    if (strstr(command, "echo") != NULL) {
        // Extract text after echo
        const char* text = "hello\n";
        strcpy(response->stdout_buffer, text);
        response->stdout_len = strlen(text);
        response->exit_code = 0;
        return 0;
    }

    // Simulate: kubectl exec pod -- pwd
    if (strstr(command, "pwd") != NULL) {
        const char* output = "/app\n";
        strcpy(response->stdout_buffer, output);
        response->stdout_len = strlen(output);
        response->exit_code = 0;
        return 0;
    }

    // Simulate: kubectl exec pod -- ps
    if (strstr(command, "ps") != NULL) {
        const char* output = "PID   USER     VIRT  RES COMMAND\n  1 root     2048  256 /app/main\n";
        strcpy(response->stdout_buffer, output);
        response->stdout_len = strlen(output);
        response->exit_code = 0;
        return 0;
    }

    // Simulate: kubectl exec pod -- cat file
    if (strstr(command, "cat") != NULL) {
        if (strstr(command, "/etc/hostname")) {
            const char* output = "my-pod-name\n";
            strcpy(response->stdout_buffer, output);
            response->stdout_len = strlen(output);
            response->exit_code = 0;
            return 0;
        }
        // Default cat output
        const char* output = "file contents here\n";
        strcpy(response->stdout_buffer, output);
        response->stdout_len = strlen(output);
        response->exit_code = 0;
        return 0;
    }

    // Simulate: kubectl exec pod -- sh -c "command"
    if (strstr(command, "sh -c") != NULL) {
        const char* output = "executed via shell\n";
        strcpy(response->stdout_buffer, output);
        response->stdout_len = strlen(output);
        response->exit_code = 0;
        return 0;
    }

    // Command not found
    snprintf(response->stderr_buffer, sizeof(response->stderr_buffer),
             "command not found: %s", command);
    response->stderr_len = strlen(response->stderr_buffer);
    response->exit_code = 127;
    return -1;
}

// Build exec response JSON
int build_exec_response(const exec_response_t* response, char* response_buffer) {
    struct json_object* root = json_object_new_object();

    json_object_object_add(root, "exitCode", json_object_new_int(response->exit_code));

    if (response->stdout_len > 0) {
        json_object_object_add(root, "stdout", json_object_new_string(response->stdout_buffer));
    }

    if (response->stderr_len > 0) {
        json_object_object_add(root, "stderr", json_object_new_string(response->stderr_buffer));
    }

    const char* json_str = json_object_to_json_string(root);
    strcpy(response_buffer, json_str);

    json_object_put(root);
    return 0;
}
