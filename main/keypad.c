#include <stdio.h>
#include <string.h>
#include <stdlib.h>
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

// Bản đồ phím phụ (chế độ đặc biệt) - ĐÃ CẬP NHẬT: Thêm '^' ở index 0
const char secondary_key_map[10] = {
    '^',    // 0: Lũy thừa (thay space)
    's',    // 1: sin( (degrees)
    'r',    // 2: root(
    'l',    // 3: ln(
    '(',    // 4: (
    ')',    // 5: )
    'p',    // 6: pi
    'e',    // 7: e
    ':',    // 8: Colon
    'S'     // 9: s_( (radians)
};

// Hằng số toán học tự định nghĩa
#define PI 3.14159265358979323846
#define E 2.71828182845904523536

// Biến toàn cục
char display_buffer[80] = "";      // Bộ đệm biểu thức
char result_str[40] = "";          // Bộ đệm kết quả chính
char error_str[40] = "";           // Bộ đệm sai số cho tích phân
int showing_result = 0;            // Cờ hiển thị kết quả (1 = true, 0 = false)
char last_key = '\0';              // Phím cuối cùng được nhấn
char prev_key = '\0';              // Phím trước đó
int secondary_mode_active = 0;     // Cờ đang trong chế độ phụ (1 = true, 0 = false)
int tertiary_mode_active = 0;      // Chế độ bàn phím thứ 3 (1 = true, 0 = false)
int display_offset = 0;            // Vị trí bắt đầu hiển thị
int cursor_pos = 0;                // Vị trí con trỏ trong chuỗi
char last_input[80] = "";          // Lưu biểu thức vừa nhập
char saved_result[40] = "";        // Lưu kết quả vừa tính

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

// Hàm tính giá trị tuyệt đối
double my_fabs(double x) {
    return (x < 0) ? -x : x;
}

// Hàm tính lũy thừa - ĐÃ CẬP NHẬT: Hỗ trợ số mũ thập phân
double my_pow(double base, double exponent) {
    // Xử lý trường hợp đặc biệt
    if (base == 0 && exponent > 0) return 0;
    if (base == 0 && exponent <= 0) return 0.0/0.0; // NaN
    if (exponent == 0) return 1.0;
    
    // Kiểm tra số mũ nguyên
    int int_exp = (int)exponent;
    if (int_exp == exponent) {
        // Số mũ nguyên
        double result = 1.0;
        int abs_exp = (int_exp < 0) ? -int_exp : int_exp;
        
        for (int i = 0; i < abs_exp; i++) {
            result *= base;
        }
        
        return (int_exp < 0) ? 1.0 / result : result;
    }
    
    // Số mũ thực: sử dụng exp(exponent * ln(base))
    // Tính ln(base)
    double z = (base - 1) / (base + 1);
    double ln_base = 0.0;
    int terms = 20;
    for (int n = 0; n < terms; n++) {
        double term = my_pow(z, 2 * n + 1) / (2 * n + 1);
        ln_base += term;
    }
    ln_base *= 2;
    
    // Tính exponent * ln(base)
    double product = exponent * ln_base;
    
    // Tính exp(product) bằng chuỗi Taylor
    double exp_result = 1.0;
    double term = 1.0;
    for (int n = 1; n < 20; n++) {
        term *= product / n;
        exp_result += term;
    }
    
    return exp_result;
}

// Hàm tính giai thừa
double factorial(int n) {
    if (n == 0) return 1.0;
    
    double result = 1.0;
    for (int i = 1; i <= n; i++) {
        result *= i;
    }
    return result;
}

// Hàm tính sin sử dụng chuỗi Maclaurin (độ)
double my_sin_deg(double x_deg) {
    // Chuyển đổi độ sang radian
    double x_rad = x_deg * PI / 180.0;
    
    // Chuẩn hóa góc về khoảng [0, 2π)
    while (x_rad < 0) x_rad += 2 * PI;
    while (x_rad >= 2 * PI) x_rad -= 2 * PI;
    
    double result = 0.0;
    int terms = 20;
    
    for (int n = 0; n < terms; n++) {
        double term = my_pow(-1, n) * my_pow(x_rad, 2 * n + 1) / factorial(2 * n + 1);
        result += term;
    }
    return result;
}

// Hàm tính sin sử dụng chuỗi Maclaurin (radian)
double my_sin_rad(double x_rad) {
    // Chuẩn hóa góc về khoảng [0, 2π)
    while (x_rad < 0) x_rad += 2 * PI;
    while (x_rad >= 2 * PI) x_rad -= 2 * PI;
    
    double result = 0.0;
    int terms = 20;
    
    for (int n = 0; n < terms; n++) {
        double term = my_pow(-1, n) * my_pow(x_rad, 2 * n + 1) / factorial(2 * n + 1);
        result += term;
    }
    return result;
}

// Hàm tính căn bậc 2
double my_sqrt(double x) {
    if (x < 0) return -1;
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

// Hàm tính logarit tự nhiên
double my_log(double x) {
    if (x <= 0) return -1;
    
    double z = (x - 1) / (x + 1);
    double result = 0.0;
    int terms = 20;
    
    for (int n = 0; n < terms; n++) {
        double term = my_pow(z, 2 * n + 1) / (2 * n + 1);
        result += term;
    }
    
    return 2 * result;
}

// Hàm đánh giá biểu thức con - ĐÃ CẬP NHẬT: Thêm xử lý toán tử '^'
double evaluate_sub_expression(char* expr) {
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
        
        if (*ptr == '-' && (ptr == expr || 
                           *(ptr-1) == '(' || 
                           *(ptr-1) == '+' || 
                           *(ptr-1) == '-' || 
                           *(ptr-1) == '*' || 
                           *(ptr-1) == '/' ||
                           *(ptr-1) == '^')) {
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
        else if (*ptr == '+' || *ptr == '-' || *ptr == '*' || *ptr == '/' || *ptr == '^') {
            ops[op_count++] = *ptr++;
        }
        else {
            ptr++;
        }
    }
    
    // Xử lý toán tử lũy thừa (phải sang trái)
    for (int i = op_count - 1; i >= 0; i--) {
        if (ops[i] == '^') {
            double left = tokens[i];
            double right = tokens[i+1];
            double calc_result = my_pow(left, right);
            
            tokens[i] = calc_result;
            
            for (int j = i+1; j < token_count-1; j++) {
                tokens[j] = tokens[j+1];
            }
            token_count--;
            
            for (int j = i; j < op_count-1; j++) {
                ops[j] = ops[j+1];
            }
            op_count--;
        }
    }
    
    // Xử lý nhân và chia
    for (int i = 0; i < op_count; ) {
        if (ops[i] == '*' || ops[i] == '/') {
            double left = tokens[i];
            double right = tokens[i+1];
            double calc_result;
            
            if (ops[i] == '*') {
                calc_result = left * right;
            } else {
                if (right == 0) {
                    return 0.0 / 0.0; // NaN
                }
                calc_result = left / right;
            }
            
            tokens[i] = calc_result;
            
            for (int j = i+1; j < token_count-1; j++) {
                tokens[j] = tokens[j+1];
            }
            token_count--;
            
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

// Hàm định dạng kết quả với tối đa 7 chữ số sau dấu chấm
void format_result(char* result) {
    char* dot = strchr(result, '.');
    if (dot == NULL) return;
    
    char* end = result + strlen(result) - 1;
    
    while (end > dot && *end == '0') {
        *end = '\0';
        end--;
    }
    
    if (*end == '.') {
        *end = '\0';
    }
}

// Hàm đánh giá một phần biểu thức đơn lẻ (phiên bản an toàn)
void evaluate_single_expression_safe(const char* expr, char* result, size_t size) {
    char work_buffer[80];
    strncpy(work_buffer, expr, sizeof(work_buffer));
    work_buffer[sizeof(work_buffer) - 1] = '\0';

    // Xử lý hằng số
    char* pi_ptr;
    while ((pi_ptr = strstr(work_buffer, "pi"))) {
        char new_buffer[80] = "";
        int prefix_len = pi_ptr - work_buffer;
        strncat(new_buffer, work_buffer, prefix_len);
        char num_str[20];
        snprintf(num_str, sizeof(num_str), "%.7f", PI);
        strcat(new_buffer, num_str);
        strcat(new_buffer, pi_ptr + 2);
        strcpy(work_buffer, new_buffer);
    }
    
    char* e_ptr;
    while ((e_ptr = strstr(work_buffer, "e"))) {
        if ((e_ptr > work_buffer && (e_ptr[-1] >= '0' && e_ptr[-1] <= '9')) || 
            (e_ptr[1] != '\0' && (e_ptr[1] >= '0' && e_ptr[1] <= '9'))) {
            break;
        }
        char new_buffer[80] = "";
        int prefix_len = e_ptr - work_buffer;
        strncat(new_buffer, work_buffer, prefix_len);
        char num_str[20];
        snprintf(num_str, sizeof(num_str), "%.7f", E);
        strcat(new_buffer, num_str);
        strcat(new_buffer, e_ptr + 1);
        strcpy(work_buffer, new_buffer);
    }

    // Xử lý các hàm toán học
    char* func_ptr;
    while ((func_ptr = strstr(work_buffer, "sin(")) || 
           (func_ptr = strstr(work_buffer, "s_(")) || 
           (func_ptr = strstr(work_buffer, "root(")) || 
           (func_ptr = strstr(work_buffer, "ln("))) {
        int func_type;
        int func_len;
        
        if (strncmp(func_ptr, "sin(", 4) == 0) {
            func_type = 1; // sin(degrees)
            func_len = 4;
        } else if (strncmp(func_ptr, "s_(", 3) == 0) {
            func_type = 4; // sin(radians)
            func_len = 3;
        } else if (strncmp(func_ptr, "root(", 5) == 0) {
            func_type = 2; // sqrt
            func_len = 5;
        } else if (strncmp(func_ptr, "ln(", 3) == 0) {
            func_type = 3; // ln
            func_len = 3;
        } else {
            break;
        }
        
        int paren_level = 1;
        char* start_ptr = func_ptr + func_len - 1;
        char* end_ptr = start_ptr + 1;
        
        while (*end_ptr && paren_level > 0) {
            if (*end_ptr == '(') paren_level++;
            else if (*end_ptr == ')') paren_level--;
            end_ptr++;
        }
        
        if (paren_level != 0) {
            strcpy(work_buffer, "Error: Missing )");
            break;
        }
        
        int sub_len = (end_ptr - start_ptr - 2);
        char sub_expr[80];
        strncpy(sub_expr, start_ptr + 1, sub_len);
        sub_expr[sub_len] = '\0';
        
        char sub_result[40];
        evaluate_single_expression_safe(sub_expr, sub_result, sizeof(sub_result));
        
        if (strstr(sub_result, "Error") != NULL) {
            strcpy(work_buffer, sub_result);
            break;
        }
        double sub_value = atof(sub_result);
        double func_value;
        
        switch (func_type) {
            case 1: func_value = my_sin_deg(sub_value); break;
            case 4: func_value = my_sin_rad(sub_value); break;
            case 2: 
                if (sub_value < 0) {
                    strcpy(work_buffer, "Error: Neg sqrt");
                    goto end_loop;
                }
                func_value = my_sqrt(sub_value); 
                break;
            case 3: 
                if (sub_value <= 0) {
                    strcpy(work_buffer, "Error: Inv log");
                    goto end_loop;
                }
                func_value = my_log(sub_value); 
                break;
            default: func_value = 0;
        }
        
        char new_buffer[80] = "";
        int prefix_len = func_ptr - work_buffer;
        strncat(new_buffer, work_buffer, prefix_len);
        
        char num_str[20];
        snprintf(num_str, sizeof(num_str), "%.7f", func_value);
        format_result(num_str);
        strcat(new_buffer, num_str);
        strcat(new_buffer, end_ptr);
        
        strcpy(work_buffer, new_buffer);
    }
    end_loop:

    // Xử lý các ngoặc đơn
    char* open_ptr;
    while ((open_ptr = strchr(work_buffer, '(')) != NULL) {
        int paren_level = 1;
        char* close_ptr = open_ptr + 1;
        
        while (*close_ptr && paren_level > 0) {
            if (*close_ptr == '(') paren_level++;
            else if (*close_ptr == ')') paren_level--;
            close_ptr++;
        }
        
        if (paren_level != 0) {
            strcpy(work_buffer, "Error: Missing )");
            break;
        }
        
        int sub_len = (close_ptr - open_ptr - 2);
        char sub_expr[80];
        strncpy(sub_expr, open_ptr + 1, sub_len);
        sub_expr[sub_len] = '\0';
        
        char sub_result[40];
        evaluate_single_expression_safe(sub_expr, sub_result, sizeof(sub_result));
        
        if (strstr(sub_result, "Error") != NULL) {
            strcpy(work_buffer, sub_result);
            break;
        }
        double sub_value = atof(sub_result);
        
        if (sub_value != sub_value) {
            strcpy(work_buffer, "Error: Div/0");
            break;
        }
        
        char new_buffer[80] = "";
        int prefix_len = open_ptr - work_buffer;
        strncat(new_buffer, work_buffer, prefix_len);
        
        char num_str[20];
        snprintf(num_str, sizeof(num_str), "%.7f", sub_value);
        format_result(num_str);
        strcat(new_buffer, num_str);
        strcat(new_buffer, close_ptr);
        
        strcpy(work_buffer, new_buffer);
    }

    // Đánh giá biểu thức cuối cùng
    double final_value = evaluate_sub_expression(work_buffer);
    
    if (final_value != final_value) {
        strcpy(work_buffer, "Error: Div/0");
    } else if (strstr(work_buffer, "Error") == NULL) {
        snprintf(work_buffer, sizeof(work_buffer), "%.7f", final_value);
        format_result(work_buffer);
    }
    
    strncpy(result, work_buffer, size);
    result[size-1] = '\0';
}

// Hàm đánh giá biểu thức
char* evaluate_expression(const char* expr) {
    static char final_result[80] = "";
    final_result[0] = '\0';
    
    char work_expr[80];
    strncpy(work_expr, expr, sizeof(work_expr));
    work_expr[sizeof(work_expr) - 1] = '\0';
    
    char* part = strtok(work_expr, ":");
    while (part != NULL) {
        char part_result[40];
        evaluate_single_expression_safe(part, part_result, sizeof(part_result));
        
        if (strstr(part_result, "Error") != NULL) {
            strcpy(final_result, part_result);
            return final_result;
        }
        
        if (final_result[0] != '\0') {
            strcat(final_result, ":");
        }
        strcat(final_result, part_result);
        
        part = strtok(NULL, ":");
    }
    
    return final_result;
}

// Hàm chèn ký tự tại vị trí con trỏ
void insert_char_at_cursor(char c) {
    size_t len = strlen(display_buffer);
    if (len >= sizeof(display_buffer) - 1) return;
    
    if (cursor_pos < len) {
        memmove(&display_buffer[cursor_pos+1], &display_buffer[cursor_pos], len - cursor_pos);
    }
    display_buffer[cursor_pos] = c;
    display_buffer[len+1] = '\0';
    cursor_pos++;
}

// Hàm chèn chuỗi tại vị trí con trỏ
void insert_string_at_cursor(const char* str) {
    size_t len = strlen(display_buffer);
    size_t str_len = strlen(str);
    
    if (len + str_len >= sizeof(display_buffer)) return;
    
    // Dời các ký tự phía sau để chèn
    if (cursor_pos < len) {
        memmove(&display_buffer[cursor_pos+str_len], &display_buffer[cursor_pos], len - cursor_pos);
    }
    
    // Chèn chuỗi
    memcpy(&display_buffer[cursor_pos], str, str_len);
    display_buffer[len + str_len] = '\0';
    cursor_pos += str_len;
}

// Hàm thay thế 'x' bằng giá trị số
void replace_x(const char* src, double value, char* dest) {
    char val_str[20];
    snprintf(val_str, sizeof(val_str), "%.7f", value);
    format_result(val_str);
    
    int j = 0;
    for (int i = 0; src[i] != '\0' && j < 79; i++) {
        if (src[i] == 'x') {
            strcpy(&dest[j], val_str);
            j += strlen(val_str);
        } else {
            dest[j++] = src[i];
        }
    }
    dest[j] = '\0';
}

// Hàm tính tích phân bằng phương pháp hình thang
double trapezoidal_integration(char* expr, double a, double b, double h) {
    int n = (int)((b - a) / h);
    if (n <= 0) n = 1;
    
    double sum = 0.0;
    char work_expr[80];
    char result_buf[40];

    // Điểm đầu
    replace_x(expr, a, work_expr);
    evaluate_single_expression_safe(work_expr, result_buf, sizeof(result_buf));
    double fa = atof(result_buf);
    
    // Điểm cuối
    replace_x(expr, b, work_expr);
    evaluate_single_expression_safe(work_expr, result_buf, sizeof(result_buf));
    double fb = atof(result_buf);
    
    sum = fa + fb;

    // Các điểm ở giữa
    for (int i = 1; i < n; i++) {
        double x = a + i * h;
        replace_x(expr, x, work_expr);
        evaluate_single_expression_safe(work_expr, result_buf, sizeof(result_buf));
        double fx = atof(result_buf);
        sum += 2.0 * fx;
    }

    return sum * h / 2.0;
}

// Hàm xử lý biểu thức tích phân - ĐÃ SỬA LỖI: Xử lý khoảng [a,b] với biểu thức
void handle_integral(char* expr) {
    // Tìm vị trí dấu ngoặc vuông
    char* open_bracket = strchr(expr, '[');
    char* close_bracket = strchr(expr, ']');
    if (!open_bracket || !close_bracket) {
        strcpy(result_str, "Invalid [a,b]");
        error_str[0] = '\0';
        return;
    }

    // Trích xuất phần trong [a,b]
    char range[40];
    strncpy(range, open_bracket + 1, close_bracket - open_bracket - 1);
    range[close_bracket - open_bracket - 1] = '\0';

    // Tìm dấu phẩy phân tách a và b
    char* comma = strchr(range, ',');
    if (!comma) {
        strcpy(result_str, "Missing comma");
        error_str[0] = '\0';
        return;
    }

    // Tách a và b
    char a_str[20], b_str[20];
    strncpy(a_str, range, comma - range);
    a_str[comma - range] = '\0';
    strcpy(b_str, comma + 1);

    // Đánh giá a và b
    char a_eval[40], b_eval[40];
    evaluate_single_expression_safe(a_str, a_eval, sizeof(a_eval));
    evaluate_single_expression_safe(b_str, b_eval, sizeof(b_eval));

    if (strstr(a_eval, "Error") || strstr(b_eval, "Error")) {
        strcpy(result_str, "Invalid a/b expr");
        error_str[0] = '\0';
        return;
    }

    double a = atof(a_eval);
    double b = atof(b_eval);

    // Tìm vị trí dấu ngoặc đơn mở sau ']'
    char* open_paren = strchr(close_bracket, '(');
    if (open_paren == NULL) {
        strcpy(result_str, "Missing (");
        error_str[0] = '\0';
        return;
    }
    
    // Tìm vị trí dấu ngoặc đơn đóng tương ứng
    int paren_level = 1;
    char* close_paren = open_paren + 1;
    while (*close_paren && paren_level > 0) {
        if (*close_paren == '(') paren_level++;
        else if (*close_paren == ')') paren_level--;
        close_paren++;
    }
    
    if (paren_level != 0) {
        strcpy(result_str, "Missing )");
        error_str[0] = '\0';
        return;
    }
    
    // Trích xuất biểu thức hàm
    int func_len = (close_paren - open_paren - 1);
    if (func_len <= 0 || func_len > 59) {
        strcpy(result_str, "Invalid func");
        error_str[0] = '\0';
        return;
    }
    
    char f_expr[60];
    strncpy(f_expr, open_paren + 1, func_len);
    f_expr[func_len] = '\0';
    
    // Tìm và loại bỏ dấu ngoặc đóng cuối cùng nếu có
    char* last_paren = strrchr(f_expr, ')');
    if (last_paren) {
        *last_paren = '\0';
    }
    
        double h = 0.001;
    double result = trapezoidal_integration(f_expr, a, b, h);
    
    // Tính sai số với h/2
    double result_half = trapezoidal_integration(f_expr, a, b, h/2);
    double error = my_fabs(result - result_half);
    
    // Định dạng kết quả
    snprintf(result_str, sizeof(result_str), "%.7f", result);
    format_result(result_str);
    result_str[sizeof(result_str)-1] = '\0';
    
    // Định dạng sai số
    snprintf(error_str, sizeof(error_str), "R:%.4e", error);
    error_str[sizeof(error_str)-1] = '\0';
}

// Xử lý phím được nhấn - ĐÃ SỬA LỖI: Chèn hàm đúng vị trí trong mode 2
void handle_key(char key) {
    if (key == '\0') return;

    // Kích hoạt chế độ bàn phím thứ 3 khi nhấn '*' hai lần
    if (key == '*' && prev_key == '*') {
        size_t len = strlen(display_buffer);
        if (len >= 2 && display_buffer[len-1] == '*' && display_buffer[len-2] == '*') {
            display_buffer[len-2] = '\0';
            cursor_pos = len - 2;
        }
        tertiary_mode_active = 1;  // true -> 1
        secondary_mode_active = 0; // false -> 0
        prev_key = '\0';
        last_key = '\0';
        return;
    }

    // Xử lý chế độ bàn phím thứ 3
    if (tertiary_mode_active) {
        if (key == '*' && prev_key == '*') {
            tertiary_mode_active = 0; // false
            prev_key = '\0';
            last_key = '\0';
            return;
        }
        
        if (key == '.' && prev_key == '.') {
            tertiary_mode_active = 0; // false
            secondary_mode_active = 1; // true
            prev_key = '\0';
            last_key = '\0';
            return;
        }
        
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
                
            case '1': // Phục hồi biểu thức trước đó
                if (strlen(last_input)) {
                    strcpy(display_buffer, last_input);
                    cursor_pos = strlen(display_buffer);
                    showing_result = 0; // false
                }
                break;
                
            case '3': // Chèn ký tự ẩn 'x'
                insert_char_at_cursor('x');
                break;
                
            case '2': // Kích hoạt chế độ tích phân
                insert_string_at_cursor("[0,0](");
                cursor_pos = strlen(display_buffer) - 1;
                break;
                
            case '7': // Lưu kết quả vừa tính
                if (strlen(saved_result)) {
                    insert_string_at_cursor(saved_result);
                }
                break;
                
            case '8': // Chèn sai số tích phân
                if (strlen(error_str)) {
                    char *newError = error_str +2; // Bỏ qua "R:"
                    insert_string_at_cursor(newError);
                }
                break;
        }
        
        prev_key = last_key;
        last_key = key;
        return;
    }
    
    // Xử lý chế độ bàn phím phụ - ĐÃ SỬA LỖI: Sử dụng insert_string_at_cursor
    if (secondary_mode_active) {
        if (key >= '0' && key <= '9') {
            int index = key - '0';
            char special_char = secondary_key_map[index];
            
            switch (special_char) {
                case 's': insert_string_at_cursor("sin("); break;
                case 'S': insert_string_at_cursor("s_("); break;
                case 'r': insert_string_at_cursor("root("); break;
                case 'l': insert_string_at_cursor("ln("); break;
                case 'p': insert_string_at_cursor("pi"); break;
                case 'e': insert_string_at_cursor("e"); break;
                case '^': insert_char_at_cursor('^'); break;
                default: insert_char_at_cursor(special_char); break;
            }
        }
        
        secondary_mode_active = 0; // false
        prev_key = last_key;
        last_key = key;
        return;
    }

    // Kích hoạt chế độ bàn phím phụ khi nhấn '.' hai lần
    if (key == '.' && prev_key == '.') {
        size_t len = strlen(display_buffer);
        if (len > 0 && display_buffer[len-1] == '.') {
            display_buffer[len-1] = '\0';
            cursor_pos = len - 1;
        }
        secondary_mode_active = 1; // true
        prev_key = '\0';
        last_key = '\0';
        return;
    }

    // Clear khi nhấn '/' hai lần liên tiếp
    if (key == '/' && prev_key == '/') {
        display_buffer[0] = '\0';
        result_str[0] = '\0';
        error_str[0] = '\0';
        showing_result = 0; // false
        last_key = '\0';
        prev_key = '\0';
        secondary_mode_active = 0; // false
        tertiary_mode_active = 0; // false
        cursor_pos = 0;
        display_offset = 0;
        printf("Cleared\n");
        return;
    }

    // Xử lý dấu thập phân
    if (key == '.') {
        if (showing_result) {
            strcpy(display_buffer, ".");
            showing_result = 0; // false
            cursor_pos = 1;
        } else {
            size_t len = strlen(display_buffer);
            int can_add = 1; // true
            int i;
            for (i = len - 1; i >= 0; i--) {
                if (display_buffer[i] == '.') {
                    can_add = 0; // false
                    break;
                } else if (display_buffer[i] < '0' || display_buffer[i] > '9') {
                    break;
                }
            }
            if (can_add) {
                if (cursor_pos == len) {
                    if (len < sizeof(display_buffer) - 1) {
                        display_buffer[len] = '.';
                        display_buffer[len + 1] = '\0';
                        cursor_pos++;
                    }
                } else {
                    insert_char_at_cursor('.');
                }
            }
        }
        prev_key = last_key;
        last_key = key;
        return;
    }

    if (key == '=') {
        if (strlen(display_buffer)) {
            strncpy(last_input, display_buffer, sizeof(last_input));
            last_input[sizeof(last_input) - 1] = '\0';
            
            if (display_buffer[0] == '[') {
                handle_integral(display_buffer);
                
                // Lưu kết quả thành công
                if (strstr(result_str, "Error") == NULL && strstr(result_str, "Invalid") == NULL) {
                    strcpy(saved_result, result_str);
                }
                
                showing_result = 1; // true
                cursor_pos = strlen(display_buffer);
            } else {
                char* result = evaluate_expression(display_buffer);
                strncpy(result_str, result, sizeof(result_str));
                result_str[sizeof(result_str) - 1] = '\0';
                error_str[0] = '\0';
                showing_result = 1; // true
                cursor_pos = strlen(display_buffer);
                
                // Lưu kết quả thành công
                if (strstr(result_str, "Error") == NULL) {
                    strcpy(saved_result, result_str);
                }
            }
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
            showing_result = 0; // false
            cursor_pos = 1;
        } else {
            if (cursor_pos == strlen(display_buffer)) {
                size_t len = strlen(display_buffer);
                if (len < sizeof(display_buffer) - 1) {
                    display_buffer[len] = key;
                    display_buffer[len + 1] = '\0';
                    cursor_pos++;
                }
            } else {
                insert_char_at_cursor(key);
            }
        }
        prev_key = last_key;
        last_key = key;
        return;
    }
    
    // Xử lý toán tử
    if (key == '+' || key == '-' || key == '*' || key == '/') {
        if (showing_result) {
            strcpy(display_buffer, result_str);
            cursor_pos = strlen(display_buffer);
            showing_result = 0; // false
        }
        
        if (cursor_pos == strlen(display_buffer)) {
            size_t len = strlen(display_buffer);
            if (len < sizeof(display_buffer) - 1) {
                display_buffer[len] = key;
                display_buffer[len + 1] = '\0';
                cursor_pos++;
            }
        } else {
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
            showing_result = 0; // false
            cursor_pos = 1;
        } else {
            if (cursor_pos == strlen(display_buffer)) {
                size_t len = strlen(display_buffer);
                if (len < sizeof(display_buffer) - 1) {
                    display_buffer[len] = key;
                    display_buffer[len + 1] = '\0';
                    cursor_pos++;
                }
            } else {
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
            
            // Cập nhật LCD
            lcd_clear();
            char lcd_line[17];
            
            int len = strlen(display_buffer);
            if (len > 16) {
                if (cursor_pos < display_offset) {
                    display_offset = cursor_pos;
                } else if (cursor_pos >= display_offset + 16) {
                    display_offset = cursor_pos - 15;
                }
                
                strncpy(lcd_line, display_buffer + display_offset, 16);
            } else {
                strncpy(lcd_line, display_buffer, 16);
                display_offset = 0;
            }
            lcd_line[16] = '\0';
            lcd_put_cur(0, 0);
            lcd_send_string(lcd_line);
            
            // Dòng 2
            if (tertiary_mode_active) {
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
                // Hiển thị kết quả tích phân
                if (display_buffer[0] == '[' && strlen(error_str) > 0) {
                    
                    // Dòng 1: kết quả chính
                    strncpy(lcd_line, result_str, 16);
                    lcd_line[16] = '\0';
                    lcd_clear();
                    lcd_put_cur(0, 0);
                    lcd_send_string(lcd_line);
                    // Dòng 2: sai số
                    lcd_put_cur(1, 0); 
                    lcd_send_string(error_str);
                } else {
                    // Hiển thị kết quả thông thường
                    strncpy(lcd_line, result_str, 16);
                    lcd_line[16] = '\0';
                    lcd_put_cur(1, 0);
                    lcd_send_string(lcd_line);
                }
            } else {
                char cursor_line[17] = "                ";
                int cursor_screen_pos = cursor_pos - display_offset;
                if (cursor_screen_pos >= 0 && cursor_screen_pos < 16) {
                    cursor_line[cursor_screen_pos] = '_';
                }
                lcd_put_cur(1, 0);
                lcd_send_string(cursor_line);
            }
            
            // In kết quả ra console
            if (showing_result) {
                printf("Result: %s\n", result_str);
                if (display_buffer[0] == '[') {
                    printf("Error Estimate: %s\n", error_str);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}