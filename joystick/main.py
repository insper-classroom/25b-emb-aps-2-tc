#!/usr/bin/env python3

import sys
import glob
import serial
import threading  # Importa a biblioteca de threading
import tkinter as tk
from tkinter import ttk
from tkinter import messagebox
import vgamepad as vg  # Importa o vgamepad
from time import sleep, time

# --- Constantes de Mapeamento ---
# Pedal Aceleredador (Potenciômetro): 7-4095
THROTTLE_MIN_RAW = 7
THROTTLE_MAX_RAW = 4095

# Pedal Freio (Potenciômetro): 0-4095
BRAKE_MIN_RAW = 0
BRAKE_MAX_RAW = 4095

# Volante (Encoder): 
STEERING_MIN_RAW = -600
STEERING_MAX_RAW = 600
# ---------------------------------

# --- Controles ---
# 0 - Encoder do volante
# 1 e 2 - Potenciômetros de acelerador e freio, respectivamente
# 3 e 4 - Botões de upshift e downshift, respectivamente

def map_value(value, in_min, in_max, out_min, out_max):
    """Mapeia linearmente um valor de um range para outro."""
    # Evita divisão por zero
    if in_max == in_min:
        return out_min
    
    # Trava o valor dentro do range de entrada
    value = max(in_min, min(value, in_max))
    
    # Mapeamento
    in_span = in_max - in_min
    out_span = out_max - out_min
    
    scaled_value = float(value) / float(in_span)
    return scaled_value


def atualizar_joystick(gamepad, control_id, value):
    """Atualiza o joystick virtual com base no controle e valor."""
    
    if control_id == 0:  # Volante (Encoder)
        # Mapeia o range do encoder (ex: -1000 a 1000) para o eixo X (-1.0 a 1.0)
        mapped_val = map_value(value, STEERING_MIN_RAW, STEERING_MAX_RAW, -1.0, 1.0) * - 2.0
        gamepad.left_joystick_float(x_value_float=mapped_val, y_value_float=0.0)
        #print(f"Volante: {value} -> {mapped_val:.2f}")

    elif control_id == 1:  # Pedal Acelerador (Potenciômetro)
        # Mapeia o range do ADC (7-4095) para o gatilho direito (0.0 a 1.0)
        mapped_val = map_value(value, THROTTLE_MIN_RAW, THROTTLE_MAX_RAW, 0.0, 1.0)
        gamepad.right_trigger_float(value_float=mapped_val)
        #print(f"Pedal: {value} -> {mapped_val:.2f}")

    elif control_id == 2:  # Pedal Freio (Potenciômetro)
        # Mapeia o range do ADC (7-4095) para o gatilho direito (0.0 a 1.0)
        mapped_val = map_value(value, THROTTLE_MIN_RAW, THROTTLE_MAX_RAW, 0.0, 1.0)
        gamepad.left_trigger_float(value_float=mapped_val)
        #print(f"Pedal: {value} -> {mapped_val:.2f}")

    elif control_id == 3: # Upshift (Botão)
        gamepad.press_button(button=vg.XUSB_BUTTON.XUSB_GAMEPAD_RIGHT_SHOULDER)
        gamepad.update()
        sleep(0.01)  # Pequena pausa para garantir o registro do botão
        gamepad.release_button(button=vg.XUSB_BUTTON.XUSB_GAMEPAD_RIGHT_SHOULDER)
        gamepad.update()
        
        #print("Upshift")

    elif control_id == 4: # Downshift (Botão)
        gamepad.press_button(button=vg.XUSB_BUTTON.XUSB_GAMEPAD_LEFT_SHOULDER)
        gamepad.update()
        sleep(0.01)  # Pequena pausa para garantir o registro do botão
        gamepad.release_button(button=vg.XUSB_BUTTON.XUSB_GAMEPAD_LEFT_SHOULDER)
        gamepad.update()

        #print("Downshift")

    # Envia a atualização para o driver do joystick
    gamepad.update()


def loop_leitura_serial(ser, gamepad, status_label, mudar_cor_circulo):
    """
    Loop principal que lê bytes da porta serial.
    Esta função roda em uma thread separada.
    """
    try:
        while True:
            # Aguardar byte de sincronização (0xFF)
            sync_byte = ser.read(size=1)
            if not sync_byte or sync_byte[0] != 0xFF:
                continue

            # Ler 3 bytes (control_id + valor(2b))
            data = ser.read(size=3)
            if len(data) < 3:
                continue
            
            # print(data)
            control_id, value = parse_data(data)

            atualizar_joystick(gamepad, control_id, value)

    except serial.SerialException as e:
        print(f"Erro na porta serial: {e}")
        status_label.config(text=f"Erro: {e}", foreground="red")
        mudar_cor_circulo("red")
    except Exception as e:
        print(f"Erro inesperado: {e}")
    finally:
        print("Thread de leitura encerrada.")


def serial_ports():
    """Retorna uma lista das portas seriais disponíveis na máquina."""
    # (Seu código original para serial_ports está bom, sem alterações)
    ports = []
    if sys.platform.startswith('win'):
        for i in range(1, 256):
            port = f'COM{i}'
            try:
                s = serial.Serial(port)
                s.close()
                ports.append(port)
            except Exception as e:
                pass
    elif sys.platform.startswith('linux') or sys.platform.startswith('cygwin'):
        ports = glob.glob('/dev/tty[A-Za-z]*')
    elif sys.platform.startswith('darwin'):
        ports = glob.glob('/dev/tty.*')
    else:
        raise EnvironmentError('Plataforma não suportada.')
    result = []
    for port in ports:
        try:
            s = serial.Serial(port)
            s.close()
            result.append(port)
        except (OSError, serial.SerialException):
            pass
    return result


def parse_data(data):
    """Interpreta os dados recebidos (control_id + valor)."""
    control_id = data[0]
    
    if control_id == 0: # Volante (Encoder) é signed
        value = int.from_bytes(data[1:3], byteorder='little', signed=True)
    if control_id == 1 or control_id == 2: # Pedal (Potenciômetro) é unsigned
        value = int.from_bytes(data[1:3], byteorder='little', signed=False)
    if control_id == 3 or control_id == 4:
        value = 1
        
    return control_id, value


def conectar_porta(port_name, root, botao_conectar, status_label, mudar_cor_circulo, gamepad):
    """Abre a conexão e INICIA A THREAD de leitura."""
    if not port_name:
        messagebox.showwarning("Aviso", "Selecione uma porta serial antes de conectar.")
        return

    try:
        ser = serial.Serial(port_name, 115200, timeout=1)
        status_label.config(text=f"Conectado em {port_name}", foreground="green")
        mudar_cor_circulo("green")
        botao_conectar.config(text="Conectado", state="disabled") # Desabilita o botão
        root.update()

        # Cria e inicia a thread de leitura
        # daemon=True faz a thread fechar quando a janela principal fechar
        thread = threading.Thread(
            target=loop_leitura_serial, 
            args=(ser, gamepad, status_label, mudar_cor_circulo), 
            daemon=True
        )
        thread.start()

    except Exception as e:
        messagebox.showerror("Erro de Conexão", f"Não foi possível conectar em {port_name}.\nErro: {e}")
        mudar_cor_circulo("red")


def criar_janela(gamepad):
    root = tk.Tk()
    root.title("Controle (Volante + Pedal)")
    root.geometry("400x250")
    root.resizable(False, False)

    # (Seu código de estilo Tkinter está bom, sem alterações)
    dark_bg = "#2e2e2e"
    dark_fg = "#ffffff"
    accent_color = "#007acc"
    root.configure(bg=dark_bg)
    style = ttk.Style(root)
    style.theme_use("clam")
    style.configure("TFrame", background=dark_bg)
    style.configure("TLabel", background=dark_bg, foreground=dark_fg, font=("Segoe UI", 11))
    style.configure("TButton", font=("Segoe UI", 10, "bold"),
                    foreground=dark_fg, background="#444444", borderwidth=0)
    style.map("TButton", background=[("active", "#555555")])
    style.configure("Accent.TButton", font=("Segoe UI", 12, "bold"),
                    foreground=dark_fg, background=accent_color, padding=6)
    style.map("Accent.TButton", background=[("active", "#005f9e")])
    style.configure("TCombobox",
                    fieldbackground=dark_bg,
                    background=dark_bg,
                    foreground=dark_fg,
                    padding=4)
    style.map("TCombobox", fieldbackground=[("readonly", dark_bg)])
    
    frame_principal = ttk.Frame(root, padding="20")
    frame_principal.pack(expand=True, fill="both")
    titulo_label = ttk.Label(frame_principal, text="Controle Volante", font=("Segoe UI", 14, "bold"))
    titulo_label.pack(pady=(0, 10))
    porta_var = tk.StringVar(value="")
    
    def mudar_cor_circulo(cor): # Definida aqui para acesso
        circle_canvas.itemconfig(circle_item, fill=cor)

    status_label = tk.Label(frame_principal, text="Aguardando seleção...", font=("Segoe UI", 11),
                            bg=dark_bg, fg=dark_fg)
    status_label.pack(pady=5)

    botao_conectar = ttk.Button(
        frame_principal,
        text="Conectar e Iniciar",
        style="Accent.TButton",
        command=lambda: conectar_porta(porta_var.get(), root, botao_conectar, status_label, mudar_cor_circulo, gamepad)
    )
    botao_conectar.pack(pady=10)

    # Frame inferior para seleção de porta
    footer_frame = ttk.Frame(root, padding="10")
    footer_frame.pack(side="bottom", fill="x")

    portas_disponiveis = serial_ports()
    if portas_disponiveis:
        porta_var.set(portas_disponiveis[0])
    port_dropdown = ttk.Combobox(footer_frame, textvariable=porta_var,
                                 values=portas_disponiveis, state="readonly", width=15)
    port_dropdown.pack(side="left", padx=5)

    circle_canvas = tk.Canvas(footer_frame, width=20, height=20, highlightthickness=0, bg=dark_bg)
    circle_item = circle_canvas.create_oval(2, 2, 18, 18, fill="red", outline="")
    circle_canvas.pack(side="right", padx=5)

    root.mainloop()


if __name__ == "__main__":
    try:
        # Inicializa o gamepad virtual (emulando um controle de Xbox 360)
        gamepad = vg.VX360Gamepad()
        print("Driver ViGEmBus encontrado. Emulando controle Xbox 360...")
    except Exception as e:
        print(f"Erro ao iniciar o vgamepad: {e}")
        print("Certifique-se de que o driver ViGEmBus está instalado.")
        print("Download: https://github.com/ViGEm/ViGEmBus/releases")
        sys.exit(1)
        
    criar_janela(gamepad)