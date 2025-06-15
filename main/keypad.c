#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c-lcd.h"

// GPIO pins cho bàn phím
#define ROW1    13
#define ROW2    12
#define ROW3    14
#define ROW4    27
#define COL1    26
#define COL2    25
#define COL3    33
#define COL4    32

// Bản đồ phím chính
const char key_map[4][4] = {
    {'1', '2', '3', '+'},
    {'4', '5', '6', '-'},
    {'7', '8', '9', '*'},
    {'.', '0', '=', '/'}
};

// Bản đồ phím phụ (chế độ đặc biệt)
const char secondary_key_map[10] = {
    ' ',    // 0: Space
    's',    // 1: sin(
    'r',    // 2: root(
    'l',    // 3: ln(
    '(',    // 4: (
    ')',    // 5: )
    'p',    // 6: pi
    'e',    // 7: e
    ':',    // 8: Colon
    'i'     // 9: i (số ảo)
};

// Hằng số toán học tự định nghĩa
#define PI 3.14159265358979323846
#define E 2.71828182845904523536

// Biến toàn cục
char display_buffer[80] = "";      // Bộ đệm biểu thức (tăng kích thước)
char result_str[40] = "";          // Bộ đệm kết quả
bool showing_result = false;       // Cờ hiển thị kết quả
char last_key = '\0';              // Phím cuối cùng được nhấn
char prev_key = '\0';              // Phím trước đó
bool secondary_mode_active = false;// Cờ đang trong chế độ phụ
bool tertiary_mode_active = false; // Chế độ bàn phím thứ 3
int display_offset = 0;            // Vị trí bắt đầu hiển thị
int cursor_pos = 0;                // Vị trí con trỏ trong chuỗi

// Hàm kiểm tra ký tự số
int isdigit(char c) {
    return (c >= '0' && c <= '9');
}

// Khởi tạo GPIO cho bàn phím
void init_keypad() {
    gpio_config_t row_config = {
        .pin_bit_mask = (1ULL << ROW1) | (1ULL << ROW2) | (1ULL << ROW3) | (1ULL << ROW4),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&row_config);

    gpio_config_t col_config = {
        .pin_bit_mask = (1ULL << COL1) | (1ULL << COL2) | (1ULL << COL3) | (1ULL << COL4),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&col_config);

    gpio_set_level(ROW1, 1);
    gpio_set_level(ROW2, 1);
    gpio_set_level(ROW3, 1);
    gpio_set_level(ROW4, 1);
}

// Quét bàn phím
char scan_keypad() {
    uint8_t row_pins[] = {ROW1, ROW2, ROW3, ROW4};
    uint8_t col_pins[] = {COL1, COL2, COL3, COL4};

    for (int row = 0; row < 4; row++) {
        gpio_set_level(row_pins[row], 0);
        for (int col = 0; col < 4; col++) {
            if (gpio_get_level(col_pins[col]) == 0) {
                vTaskDelay(pdMS_TO_TICKS(20)); // Chống dội phím
                while (gpio_get_level(col_pins[col]) == 0) {
                    vTaskDelay(10 / portTICK_PERIOD_MS);
                }
                gpio_set_level(row_pins[row], 1);
                return key_map[row][col];
            }
        }
        gpio_set_level(row_pins[row], 1);
    }
    return '\0';
}

// Hàm tính giá trị tuyệt đối (không dùng math.h)
double my_fabs(double x) {
    return (x < 0) ? -x : x;
}

// Hàm tính lũy thừa (không dùng math.h)
double my_pow(double base, int exponent) {
    if (exponent == 0) return 1.0;
    
    double result = 1.0;
    int abs_exp = (exponent < 0) ? -exponent : exponent;
    
    for (int i = 0; i < abs_exp; i++) {
        result *= base;
    }
    
    return (exponent < 0) ? 1.0 / result : result;
}

// Hàm tính giai thừa (dùng cho chuỗi Maclaurin)
double factorial(int n) {
    if (n == 0) return 1.0;
    
    double result = 1.0;
    for (int i = 1; i <= n; i++) {
        result *= i;
    }
    return result;
}

// Hàm tính sin sử dụng chuỗi Maclaurin (không dùng math.h)
double my_sin(double x_deg) {
    // Chuyển đổi độ sang radian
    double x_rad = x_deg * PI / 180.0;
    
    // Chuẩn hóa góc về khoảng [0, 2π)
    while (x_rad < 0) x_rad += 2 * PI;
    while (x_rad >= 2 * PI) x_rad -= 2 * PI;
    
    double result = 0.0;
    int terms = 20; // Số lượng số hạng
    
    for (int n = 0; n < terms; n++) {
        double term = my_pow(-1, n) * my_pow(x_rad, 2 * n + 1) / factorial(2 * n + 1);
        result += term;
    }
    return result;
}

// Hàm tính căn bậc 2 sử dụng phương pháp Newton (không dùng math.h)
double my_sqrt(double x) {
    if (x < 0) return -1; // Lỗi: không xác định cho số âm
    if (x == 0) return 0;
    
    double guess = x;
    int iterations = 20;
    
    for (int i = 0; i < iterations; i++) {
        double new_guess = 0.5 * (guess + x / guess);
        if (my_fabs(new_guess - guess) < 1e-12) break;
        guess = new_guess;
    }
    return guess;
}

// Hàm tính logarit tự nhiên sử dụng chuỗi Maclaurin (không dùng math.h)
double my_log(double x) {
    if (x <= 0) return -1; // Lỗi: không xác định
    
    // Sử dụng khai triển Taylor tại x = 1
    double z = (x - 1) / (x + 1);
    double result = 0.0;
    int terms = 20; // Số lượng số hạng
    
    for (int n = 0; n < terms; n++) {
        double term = my_pow(z, 2 * n + 1) / (2 * n + 1);
        result += term;
    }
    
    return 2 * result;
}

// Hàm đánh giá biểu thức con (hỗ trợ ngoặc đơn) - ĐÃ SỬA
double evaluate_sub_expression(char* expr) {
    // Tách các token: số, toán tử
    double tokens[20] = {0};
    char ops[20] = {0};
    int token_count = 0;
    int op_count = 0;
    
    char* ptr = expr;
    char current_token[20];
    int token_index = 0;
    
    while (*ptr) {
        if (*ptr == ' ') {
            ptr++;
            continue;
        }
        
        // Xử lý số âm
        if (*ptr == '-' && (ptr == expr || 
                           *(ptr-1) == '(' || 
                           *(ptr-1) == '+' || 
                           *(ptr-1) == '-' || 
                           *(ptr-1) == '*' || 
                           *(ptr-1) == '/')) {
            token_index = 0;
            current_token[token_index++] = *ptr++;
            
            while (isdigit(*ptr) || *ptr == '.' || 
                   (*ptr == 'E' || *ptr == 'e') || 
                   (*ptr == '-' && (*(ptr-1) == 'E' || *(ptr-1) == 'e'))) {
                current_token[token_index++] = *ptr++;
            }
            current_token[token_index] = '\0';
            tokens[token_count++] = atof(current_token);
        }
        // Xử lý số dương
        else if (isdigit(*ptr) || *ptr == '.') {
            token_index = 0;
            while (isdigit(*ptr) || *ptr == '.' || 
                   (*ptr == 'E' || *ptr == 'e') || 
                   (*ptr == '-' && (*(ptr-1) == 'E' || *(ptr-1) == 'e'))) {
                current_token[token_index++] = *ptr++;
            }
            current_token[token_index] = '\0';
            tokens[token_count++] = atof(current_token);
        } 
        else if (*ptr == '+' || *ptr == '-' || *ptr == '*' || *ptr == '/') {
            // Xử lý toán tử
            ops[op_count++] = *ptr++;
        }
        else {
            // Bỏ qua ký tự không hợp lệ
            ptr++;
        }
    }
    
    // Xử lý nhân và chia trước
    for (int i = 0; i < op_count; ) {
        if (ops[i] == '*' || ops[i] == '/') {
            double left = tokens[i];
            double right = tokens[i+1];
            double calc_result;
            
            if (ops[i] == '*') {
                calc_result = left * right;
            } else {
                if (right == 0) {
                    return 0.0 / 0.0; // NaN: báo lỗi chia cho 0
                }
                calc_result = left / right;
            }
            
            // Cập nhật mảng
            tokens[i] = calc_result;
            
            // Dịch chuyển các số còn lại
            for (int j = i+1; j < token_count-1; j++) {
                tokens[j] = tokens[j+1];
            }
            token_count--;
            
            // Dịch chuyển các toán tử còn lại
            for (int j = i; j < op_count-1; j++) {
                ops[j] = ops[j+1];
            }
            op_count--;
        } else {
            i++;
        }
    }
    
    // Xử lý cộng và trừ
    double final_result = tokens[0];
    for (int i = 0; i < op_count; i++) {
        if (ops[i] == '+') {
            final_result += tokens[i+1];
        } else if (ops[i] == '-') {
            final_result -= tokens[i+1];
        }
    }

    return final_result;
}

void format_result(char* result) {
    // Tìm dấu chấm thập phân
    char* dot = strchr(result, '.');
    if (dot == NULL) return; // Không có dấu chấm, không cần xử lý
    
    // Tìm vị trí kết thúc của số
    char* end = result + strlen(result) - 1;
    
    // Loại bỏ các số 0 ở cuối
    while (end > dot && *end == '0') {
        *end = '\0';
        end--;
    }
    
    // Nếu sau dấu chấm không còn số nào thì bỏ luôn dấu chấm
    if (*(dot + 1) == '\0') {
        *dot = '\0';
    }
}

// Hàm đánh giá một phần biểu thức đơn lẻ
char* evaluate_single_expression(const char* expr) {
    static char result[40] = "";
    char work_buffer[80];
    strncpy(work_buffer, expr, sizeof(work_buffer));
    work_buffer[sizeof(work_buffer) - 1] = '\0';

    // Xử lý các hằng số: thay thế "pi" và "e" bằng giá trị số
    char* pi_ptr;
    while ((pi_ptr = strstr(work_buffer, "pi"))) {
        char new_buffer[80] = "";
        int prefix_len = pi_ptr - work_buffer;
        strncat(new_buffer, work_buffer, prefix_len);
        char num_str[20];
        snprintf(num_str, sizeof(num_str), "%.10f", PI);
        strcat(new_buffer, num_str);
        strcat(new_buffer, pi_ptr + 2); // +2 để bỏ qua "pi"
        strcpy(work_buffer, new_buffer);
    }
    
    char* e_ptr;
    while ((e_ptr = strstr(work_buffer, "e"))) {
        // Kiểm tra xem 'e' có phải là một phần của số không (vd: 1e3) thì bỏ qua
        if ((e_ptr > work_buffer && (e_ptr[-1] >= '0' && e_ptr[-1] <= '9')) || 
            (e_ptr[1] != '\0' && (e_ptr[1] >= '0' && e_ptr[1] <= '9'))) {
            break;
        }
        char new_buffer[80] = "";
        int prefix_len = e_ptr - work_buffer;
        strncat(new_buffer, work_buffer, prefix_len);
        char num_str[20];
        snprintf(num_str, sizeof(num_str), "%.10f", E);
        strcat(new_buffer, num_str);
        strcat(new_buffer, e_ptr + 1); // bỏ qua 'e'
        strcpy(work_buffer, new_buffer);
    }

    // Xử lý các hàm toán học
    char* func_ptr;
    while ((func_ptr = strstr(work_buffer, "sin(")) || 
           (func_ptr = strstr(work_buffer, "root(")) || 
           (func_ptr = strstr(work_buffer, "ln("))) {
        int func_type;
        int func_len;
        
        if (strncmp(func_ptr, "sin(", 4) == 0) {
            func_type = 1;
            func_len = 4;
        } else if (strncmp(func_ptr, "root(", 5) == 0) {
            func_type = 2;
            func_len = 5;
        } else if (strncmp(func_ptr, "ln(", 3) == 0) {
            func_type = 3;
            func_len = 3;
        } else {
            break;
        }
        
        // Tìm ngoặc đóng tương ứng
        int paren_level = 1;
        char* start_ptr = func_ptr + func_len - 1; // trỏ tới '('
        char* end_ptr = start_ptr + 1;
        
        while (*end_ptr && paren_level > 0) {
            if (*end_ptr == '(') paren_level++;
            else if (*end_ptr == ')') paren_level--;
            end_ptr++;
        }
        
        if (paren_level != 0) {
            // Lỗi: thiếu ngoặc đóng
            strcpy(result, "Error: Missing )");
            return result;
        }
        
        // Trích xuất biểu thức trong ngoặc
        int sub_len = (end_ptr - start_ptr - 2); // bỏ qua '(' và ')' 
        char sub_expr[80];
        strncpy(sub_expr, start_ptr + 1, sub_len);
        sub_expr[sub_len] = '\0';
        
        // Đánh giá biểu thức con bằng đệ quy
        char* sub_result = evaluate_single_expression(sub_expr);
        if (strstr(sub_result, "Error") != NULL) {
            strcpy(result, sub_result);
            return result;
        }
        double sub_value = atof(sub_result);
        double func_value;
        
        switch (func_type) {
            case 1: func_value = my_sin(sub_value); break;
            case 2: 
                if (sub_value < 0) {
                    strcpy(result, "Error: Neg sqrt");
                    return result;
                }
                func_value = my_sqrt(sub_value); 
                break;
            case 3: 
                if (sub_value <= 0) {
                    strcpy(result, "Error: Inv log");
                    return result;
                }
                func_value = my_log(sub_value); 
                break;
            default: func_value = 0;
        }
        
        // Tạo chuỗi mới
        char new_buffer[80] = "";
        int prefix_len = func_ptr - work_buffer;
        strncat(new_buffer, work_buffer, prefix_len);
        
        char num_str[20];
        if (func_value == (int)func_value) {
            snprintf(num_str, sizeof(num_str), "%d", (int)func_value);
        } else {
            snprintf(num_str, sizeof(num_str), "%.10f", func_value);
        }
        strcat(new_buffer, num_str);
        strcat(new_buffer, end_ptr);
        
        strcpy(work_buffer, new_buffer);
    }

    // Xử lý các ngoặc đơn
    char* open_ptr;
    while ((open_ptr = strchr(work_buffer, '(')) != NULL) {
        // Tìm ngoặc đóng tương ứng
        int paren_level = 1;
        char* close_ptr = open_ptr + 1;
        
        while (*close_ptr && paren_level > 0) {
            if (*close_ptr == '(') paren_level++;
            else if (*close_ptr == ')') paren_level--;
            close_ptr++;
        }
        
        if (paren_level != 0) {
            strcpy(result, "Error: Missing )");
            return result;
        }
        
        // Trích xuất biểu thức trong ngoặc
        int sub_len = (close_ptr - open_ptr - 2); // bỏ qua '(' và ')' 
        char sub_expr[80];
        strncpy(sub_expr, open_ptr + 1, sub_len);
        sub_expr[sub_len] = '\0';
        
        // Đánh giá biểu thức con bằng đệ quy
        char* sub_result = evaluate_single_expression(sub_expr);
        if (strstr(sub_result, "Error") != NULL) {
            strcpy(result, sub_result);
            return result;
        }
        double sub_value = atof(sub_result);
        
        // Kiểm tra NaN (chia cho 0)
        if (sub_value != sub_value) {
            strcpy(result, "Error: Div/0");
            return result;
        }
        
        // Tạo chuỗi mới
        char new_buffer[80] = "";
        int prefix_len = open_ptr - work_buffer;
        strncat(new_buffer, work_buffer, prefix_len);
        
        char num_str[20];
        if (sub_value == (int)sub_value) {
            snprintf(num_str, sizeof(num_str), "%d", (int)sub_value);
        } else {
            snprintf(num_str, sizeof(num_str), "%.10f", sub_value);
        }
        strcat(new_buffer, num_str);
        strcat(new_buffer, close_ptr);
        
        strcpy(work_buffer, new_buffer);
    }

    // Đánh giá biểu thức cuối cùng (không còn ngoặc)
    double final_value = evaluate_sub_expression(work_buffer);
    
    // Kiểm tra NaN (chia cho 0)
    if (final_value != final_value) {
        strcpy(result, "Error: Div/0");
        return result;
    }
    
    // Định dạng kết quả
    if (final_value == (int)final_value) {
        snprintf(result, sizeof(result), "%d", (int)final_value);
    } else {
        snprintf(result, sizeof(result), "%.10f", final_value);
        format_result(result); // Định dạng lại kết quả
    }
    
    return result;
}

// Hàm đánh giá biểu thức (hỗ trợ nhiều phép tính phân cách bằng :)
char* evaluate_expression(const char* expr) {
    static char final_result[80] = "";
    final_result[0] = '\0';
    
    // Tạo bản sao của biểu thức để xử lý
    char work_expr[80];
    strncpy(work_expr, expr, sizeof(work_expr));
    work_expr[sizeof(work_expr) - 1] = '\0';
    
    // Con trỏ để tách các phần
    char* part = strtok(work_expr, ":");
    while (part != NULL) {
        // Đánh giá từng phần biểu thức
        char* part_result = evaluate_single_expression(part);
        
        // Nếu gặp lỗi, trả về lỗi ngay lập tức
        if (strstr(part_result, "Error") != NULL) {
            strcpy(final_result, part_result);
            return final_result;
        }
        
        // Thêm kết quả phần này vào kết quả cuối
        if (final_result[0] != '\0') {
            strcat(final_result, ":");
        }
        strcat(final_result, part_result);
        
        // Lấy phần tiếp theo
        part = strtok(NULL, ":");
    }
    
    return final_result;
}

// Hàm chèn ký tự tại vị trí con trỏ
void insert_char_at_cursor(char c) {
    size_t len = strlen(display_buffer);
    if (len >= sizeof(display_buffer) - 1) return;
    
    // Dời các ký tự phía sau để chèn
    if (cursor_pos < len) {
        memmove(&display_buffer[cursor_pos+1], &display_buffer[cursor_pos], len - cursor_pos);
    }
    display_buffer[cursor_pos] = c;
    display_buffer[len+1] = '\0';
    cursor_pos++;
}

// Xử lý phím được nhấn
void handle_key(char key) {
    if (key == '\0') return;

    // Kích hoạt chế độ bàn phím thứ 3 khi nhấn '*' hai lần
    if (key == '*' && prev_key == '*') {
        // Xóa cả hai dấu '*' nếu có
        size_t len = strlen(display_buffer);
        if (len >= 2 && display_buffer[len-1] == '*' && display_buffer[len-2] == '*') {
            display_buffer[len-2] = '\0';
            cursor_pos = len - 2;
        }
        tertiary_mode_active = true;
        secondary_mode_active = false;
        prev_key = '\0';
        last_key = '\0';
        return;
    }

    // Xử lý chế độ bàn phím thứ 3
    if (tertiary_mode_active) {
        // Thoát chế độ 3 khi nhấn '**'
        if (key == '*' && prev_key == '*') {
            tertiary_mode_active = false;
            prev_key = '\0';
            last_key = '\0';
            return;
        }
        
        // Chuyển sang chế độ 2 khi nhấn '..'
        if (key == '.' && prev_key == '.') {
            tertiary_mode_active = false;
            secondary_mode_active = true;
            prev_key = '\0';
            last_key = '\0';
            return;
        }
        
        // Xử lý các phím điều khiển trong chế độ 3
        switch(key) {
            case '4': // Di chuyển con trỏ sang trái
                if (cursor_pos > 0) {
                    cursor_pos--;
                    if (cursor_pos < display_offset) {
                        display_offset = cursor_pos;
                    }
                }
                break;
                
            case '6': // Di chuyển con trỏ sang phải
                if (cursor_pos < strlen(display_buffer)) {
                    cursor_pos++;
                    if (cursor_pos > display_offset + 15) {
                        display_offset = cursor_pos - 15;
                    }
                }
                break;
                
            case '5': // Xóa ký tự tại vị trí trước con trỏ
                if (cursor_pos > 0 && cursor_pos <= strlen(display_buffer)) {
                    memmove(&display_buffer[cursor_pos-1], 
                            &display_buffer[cursor_pos], 
                            strlen(display_buffer) - cursor_pos + 1);
                    cursor_pos--;
                    if (cursor_pos < display_offset) {
                        display_offset = cursor_pos;
                    }
                }
                break;
        }
        
        // Cập nhật prev_key và last_key cho tất cả các phím
        prev_key = last_key;
        last_key = key;
        return;
    }
    
    // Xử lý chế độ bàn phím phụ
    if (secondary_mode_active) {
        if (key >= '0' && key <= '9') {
            int index = key - '0';
            char special_char = secondary_key_map[index];
            
            size_t len = strlen(display_buffer);
            
            switch (special_char) {
                case 's': // sin(
                    if (len + 5 < sizeof(display_buffer)) {
                        strcat(display_buffer, "sin(");
                        cursor_pos = len + 4;
                    }
                    break;
                    
                case 'r': // root(
                    if (len + 6 < sizeof(display_buffer)) {
                        strcat(display_buffer, "root(");
                        cursor_pos = len + 5;
                    }
                    break;
                    
                case 'l': // log(
                    if (len + 4 < sizeof(display_buffer)) {
                        strcat(display_buffer, "ln(");
                        cursor_pos = len + 3;
                    }
                    break;
                    
                case 'p': // pi
                    if (len + 20 < sizeof(display_buffer)) { // đủ chỗ cho giá trị PI
                        strcat(display_buffer, "pi");
                        cursor_pos = len + 2;
                    }
                    break;
                    
                case 'e': // e
                    if (len + 20 < sizeof(display_buffer)) {
                        strcat(display_buffer, "e");
                        cursor_pos = len + 1;
                    }
                    break;
                    
                default:
                    if (len < sizeof(display_buffer) - 1) {
                        display_buffer[len] = special_char;
                        display_buffer[len + 1] = '\0';
                        cursor_pos = len + 1;
                    }
                    break;
            }
        }
        
        // Tắt chế độ phụ sau khi xử lý
        secondary_mode_active = false;
        prev_key = last_key;
        last_key = key;
        return;
    }

    // Kích hoạt chế độ bàn phím phụ khi nhấn '.' hai lần
    if (key == '.' && prev_key == '.') {
        // Xóa dấu '.' thứ hai
        size_t len = strlen(display_buffer);
        if (len > 0 && display_buffer[len-1] == '.') {
            display_buffer[len-1] = '\0';
            cursor_pos = len - 1;
        }
        secondary_mode_active = true;
        prev_key = '\0';
        last_key = '\0';
        return;
    }

    // Clear khi nhấn '/' hai lần liên tiếp
    if (key == '/' && prev_key == '/') {
        display_buffer[0] = '\0';
        result_str[0] = '\0';
        showing_result = false;
        last_key = '\0';
        prev_key = '\0';
        secondary_mode_active = false;
        tertiary_mode_active = false;
        cursor_pos = 0;
        display_offset = 0;
        printf("Cleared\n");
        return;
    }

    // Xử lý dấu thập phân (không tự động thêm '0')
    if (key == '.') {
        if (showing_result) {
            strcpy(display_buffer, ".");
            showing_result = false;
            cursor_pos = 1;
        } else {
            size_t len = strlen(display_buffer);
            
            // Kiểm tra xem có thể thêm dấu thập phân không
            bool can_add = true;
            // Duyệt ngược từ cuối chuỗi để tìm số gần nhất
            int i;
            for (i = len - 1; i >= 0; i--) {
                if (display_buffer[i] == '.') {
                    can_add = false; // đã có dấu chấm trong số hiện tại
                    break;
                } else if (display_buffer[i] < '0' || display_buffer[i] > '9') {
                    // Gặp toán tử, có thể thêm dấu chấm
                    break;
                }
            }
            // Chỉ thêm . nếu chưa có
            if (can_add) {
                if (cursor_pos == len) {
                    // Thêm vào cuối
                    if (len < sizeof(display_buffer) - 1) {
                        display_buffer[len] = '.';
                        display_buffer[len + 1] = '\0';
                        cursor_pos++;
                    }
                } else {
                    // Chèn tại vị trí con trỏ
                    insert_char_at_cursor('.');
                }
            }
        }
        prev_key = last_key;
        last_key = key;
        return;
    }

    if (key == '=') {
        if (strlen(display_buffer) > 0) {
            char* result = evaluate_expression(display_buffer);
            strncpy(result_str, result, sizeof(result_str));
            result_str[sizeof(result_str) - 1] = '\0';
            showing_result = true;
            cursor_pos = strlen(display_buffer); // Di chuyển con trỏ về cuối
            printf("Result: %s\n", result_str);
        }
        prev_key = last_key;
        last_key = key;
        return;
    }

    // Xử lý số
    if (key >= '0' && key <= '9') {
        if (showing_result) {
            display_buffer[0] = key;
            display_buffer[1] = '\0';
            showing_result = false;
            cursor_pos = 1;
        } else {
            if (cursor_pos == strlen(display_buffer)) {
                // Thêm vào cuối
                size_t len = strlen(display_buffer);
                if (len < sizeof(display_buffer) - 1) {
                    display_buffer[len] = key;
                    display_buffer[len + 1] = '\0';
                    cursor_pos++;
                }
            } else {
                // Chèn tại vị trí con trỏ
                insert_char_at_cursor(key);
            }
        }
        prev_key = last_key;
        last_key = key;
        return;
    }
    
    // Xử lý toán tử (bao gồm số âm)
    if (key == '+' || key == '-' || key == '*' || key == '/') {
        if (showing_result) {
            strcpy(display_buffer, result_str);
            cursor_pos = strlen(display_buffer);
            showing_result = false;
        }
        
        if (cursor_pos == strlen(display_buffer)) {
            // Thêm vào cuối
            size_t len = strlen(display_buffer);
            if (len < sizeof(display_buffer) - 1) {
                display_buffer[len] = key;
                display_buffer[len + 1] = '\0';
                cursor_pos++;
            }
        } else {
            // Chèn tại vị trí con trỏ
            insert_char_at_cursor(key);
        }
        
        prev_key = last_key;
        last_key = key;
        return;
    }
    
    // Xử lý ngoặc đơn
    if (key == '(' || key == ')') {
        if (showing_result) {
            display_buffer[0] = key;
            display_buffer[1] = '\0';
            showing_result = false;
            cursor_pos = 1;
        } else {
            if (cursor_pos == strlen(display_buffer)) {
                // Thêm vào cuối
                size_t len = strlen(display_buffer);
                if (len < sizeof(display_buffer) - 1) {
                    display_buffer[len] = key;
                    display_buffer[len + 1] = '\0';
                    cursor_pos++;
                }
            } else {
                // Chèn tại vị trí con trỏ
                insert_char_at_cursor(key);
            }
        }
        prev_key = last_key;
        last_key = key;
        return;
    }
}

void app_main() {
    init_keypad();
    printf("Advanced Calculator Ready!\n");
    lcd_init();
    lcd_clear();
    lcd_put_cur(0, 0);
    lcd_send_string("Calculator Ready");
    while (1) {
        char key = scan_keypad();
        if (key != '\0') {
            handle_key(key);
            printf("Expression: %s\n", display_buffer);
            
            // Cập nhật LCD - cắt chuỗi để hiển thị
            lcd_clear();
            char lcd_line[17]; // 16 ký tự + null
            
            // Tính toán vị trí hiển thị
            int len = strlen(display_buffer);
            if (len > 16) {
                // Đảm bảo con trỏ luôn hiển thị
                if (cursor_pos < display_offset) {
                    display_offset = cursor_pos;
                } else if (cursor_pos >= display_offset + 16) {
                    display_offset = cursor_pos - 15;
                }
                
                strncpy(lcd_line, display_buffer + display_offset, 16);
            } else {
                strncpy(lcd_line, display_buffer, 16);
                display_offset = 0; // Reset offset nếu chuỗi ngắn
            }
            lcd_line[16] = '\0';
            lcd_put_cur(0, 0);
            lcd_send_string(lcd_line);
            
            // Dòng 2: kết quả hoặc chế độ hoặc con trỏ
            if (tertiary_mode_active) {
                // Hiển thị vị trí con trỏ
                char cursor_line[17] = "                ";
                int cursor_screen_pos = cursor_pos - display_offset;
                if (cursor_screen_pos >= 0 && cursor_screen_pos < 16) {
                    cursor_line[cursor_screen_pos] = '^';
                }
                lcd_put_cur(1, 0);
                lcd_send_string(cursor_line);
            } else if (secondary_mode_active) {
                lcd_put_cur(1, 0);
                lcd_send_string("Secondary Mode");
            } else if (showing_result) {
                strncpy(lcd_line, result_str, 16);
                lcd_line[16] = '\0';
                lcd_put_cur(1, 0);
                lcd_send_string(lcd_line);
            } else {
                // Hiển thị vị trí con trỏ ở chế độ thường
                char cursor_line[17] = "                ";
                int cursor_screen_pos = cursor_pos - display_offset;
                if (cursor_screen_pos >= 0 && cursor_screen_pos < 16) {
                    cursor_line[cursor_screen_pos] = '_';
                }
                lcd_put_cur(1, 0);
                lcd_send_string(cursor_line);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}