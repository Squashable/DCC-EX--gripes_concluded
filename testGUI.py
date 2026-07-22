import sys
import pygame

# --- INITIALIZATION & CONSTANTS ---
pygame.init()
WIDTH, HEIGHT = 900, 500
screen = pygame.display.set_mode((WIDTH, HEIGHT))
pygame.display.set_caption("DCC-EX Dual Engine Controller Prototype")

# Colors
BG_COLOR = (24, 28, 36)
PANEL_BG = (35, 41, 52)
ACCENT_BLUE = (0, 162, 252)
ACCENT_GREEN = (46, 204, 113)
TEXT_WHITE = (240, 240, 240)
TEXT_MUTED = (140, 147, 158)
BORDER_COLOR = (60, 68, 83)

# Fonts
FONT_MAIN = pygame.font.SysFont("Consolas", 18, bold=True)
FONT_LARGE = pygame.font.SysFont("Consolas", 28, bold=True)
FONT_SMALL = pygame.font.SysFont("Consolas", 14)

# --- LOCOMOTIVE STATE ---
class EngineState:
    def __init__(self, address):
        self.address = address
        self.speed = 0  # 0 to 128
        self.direction = "FWD"  # FWD or REV
        self.active_functions = set()  # Store active function numbers e.g. {0, 2, 8}

# Instantiate Engine 1 and Engine 2
engines = {
    1: EngineState(address=4401),
    2: EngineState(address=8302)
}

active_num_engine = 1  # Which engine numpad inputs currently target (1 or 2)
num_buffer = ""        # Keypad buffer for entering new engine addresses

# --- DRAWING FUNCTIONS ---
def draw_engine_panel(surface, rect, engine_id, engine, is_focused):
    # Draw Background Container
    border_col = ACCENT_BLUE if is_focused else BORDER_COLOR
    pygame.draw.rect(surface, PANEL_BG, rect, border_radius=8)
    pygame.draw.rect(surface, border_col, rect, width=2, border_radius=8)

    x, y, w, h = rect

    # 1. Active Functions Row (Top Horizontal Bar)
    func_rect = pygame.Rect(x + 15, y + 15, w - 30, 35)
    pygame.draw.rect(surface, (20, 24, 30), func_rect, border_radius=5)
    
    # Sort functions numerically for display
    funcs_sorted = sorted(list(engine.active_functions))
    func_str = " ".join(f"F{f}" for f in funcs_sorted) if funcs_sorted else "NONE"
    
    lbl_func = FONT_SMALL.render(f"ENGINE {engine_id} FUNCS:", True, TEXT_MUTED)
    txt_func = FONT_MAIN.render(func_str, True, ACCENT_GREEN)
    surface.blit(lbl_func, (func_rect.x + 10, func_rect.y + 4))
    surface.blit(txt_func, (func_rect.x + 130, func_rect.y + 8))

    # 2. Locomotive Address (Bottom Left Box)
    loco_rect = pygame.Rect(x + 15, y + 60, 120, h - 75)
    pygame.draw.rect(surface, (20, 24, 30), loco_rect, border_radius=5)
    
    lbl_loco = FONT_SMALL.render("LOCO ID", True, TEXT_MUTED)
    txt_loco = FONT_LARGE.render(f"#{engine.address}", True, TEXT_WHITE)
    surface.blit(lbl_loco, (loco_rect.x + 10, loco_rect.y + 10))
    surface.blit(txt_loco, (loco_rect.x + 10, loco_rect.y + 35))

    # 3. Speed Bar & Direction (Bottom Right Box)
    speed_rect = pygame.Rect(x + 145, y + 60, w - 160, h - 75)
    pygame.draw.rect(surface, (20, 24, 30), speed_rect, border_radius=5)
    
    # Speed Bar Graphics
    pct = engine.speed / 128.0
    bar_x, bar_y, bar_w, bar_h = speed_rect.x + 10, speed_rect.y + 35, speed_rect.width - 20, 20
    pygame.draw.rect(surface, BORDER_COLOR, (bar_x, bar_y, bar_w, bar_h), border_radius=4)
    if pct > 0:
        pygame.draw.rect(surface, ACCENT_BLUE, (bar_x, bar_y, int(bar_w * pct), bar_h), border_radius=4)

    # Text Speed Label
    dir_color = ACCENT_GREEN if engine.direction == "FWD" else (231, 76, 60)
    txt_speed = FONT_MAIN.render(f"SPEED: {int(pct*100)}% ({engine.speed}/128)", True, TEXT_WHITE)
    txt_dir = FONT_MAIN.render(f"[{engine.direction}]", True, dir_color)
    
    surface.blit(txt_speed, (speed_rect.x + 10, speed_rect.y + 10))
    surface.blit(txt_dir, (speed_rect.x + speed_rect.width - 65, speed_rect.y + 10))


def draw_hud(surface):
    # Bottom HUD status for active numpad focus & instructions
    hud_str = f"Numpad Focus: ENGINE {active_num_engine}  |  Buffer: [{num_buffer}]"
    txt_hud = FONT_MAIN.render(hud_str, True, ACCENT_BLUE)
    surface.blit(txt_hud, (20, 395))

    instructions = [
        "CONTROLS:",
        "• Speed: Mouse Scroll UP/DOWN over an engine panel",
        "• Direction: Left-Click on speed area to toggle FWD/REV",
        "• Target Focus: Press '1' or '2' to switch numpad target",
        "• Toggle Functions: Type F-number + ENTER (e.g., type '2' + Enter to toggle F2)",
        "• Set Loco Address: Type 'A' + Number + ENTER (e.g., 'A4401' + Enter)"
    ]
    for i, line in enumerate(instructions):
        txt = FONT_SMALL.render(line, True, TEXT_MUTED)
        surface.blit(txt, (20, 420 + (i * 12)))

# --- MAIN LOOP ---
clock = pygame.time.Clock()
running = True

while running:
    # 1. Event Handling
    for event in pygame.event.get():
        if event.type == pygame.QUIT:
            running = False

        # Mouse Interaction
        elif event.type == pygame.MOUSEBUTTONDOWN:
            mx, my = event.pos
            
            # Determine which panel was clicked
            p1_rect = pygame.Rect(15, 15, 425, 360)
            p2_rect = pygame.Rect(460, 15, 425, 360)
            
            target_eng = None
            if p1_rect.collidepoint(mx, my):
                target_eng = 1
            elif p2_rect.collidepoint(mx, my):
                target_eng = 2

            if target_eng:
                active_num_engine = target_eng
                # Scroll Wheel (Speed)
                if event.button == 4:  # Scroll Up
                    engines[target_eng].speed = min(128, engines[target_eng].speed + 4)
                elif event.button == 5:  # Scroll Down
                    engines[target_eng].speed = max(0, engines[target_eng].speed - 4)
                # Left Click (Toggle Direction)
                elif event.button == 1:
                    cur_dir = engines[target_eng].direction
                    engines[target_eng].direction = "REV" if cur_dir == "FWD" else "FWD"

    # Keyboard Input
    keys = pygame.key.get_pressed()
    for event in pygame.event.get(pygame.KEYDOWN):
        if event.key == pygame.K_1 and num_buffer == "":
            active_num_engine = 1
        elif event.key == pygame.K_2 and num_buffer == "":
            active_num_engine = 2
        elif event.key == pygame.K_BACKSPACE:
            num_buffer = num_buffer[:-1]
        elif event.key == pygame.K_RETURN:
            # Process Buffer
            if num_buffer.startswith("A"):  # Change Address
                try:
                    new_addr = int(num_buffer[1:])
                    engines[active_num_engine].address = new_addr
                except ValueError:
                    pass
            elif num_buffer.isdigit():  # Toggle Function
                f_num = int(num_buffer)
                if 0 <= f_num <= 28:
                    eng = engines[active_num_engine]
                    if f_num in eng.active_functions:
                        eng.active_functions.remove(f_num)
                    else:
                        eng.active_functions.add(f_num)
            num_buffer = ""
        else:
            if len(event.unicode) > 0 and (event.unicode.isdigit() or event.unicode.upper() == 'A'):
                num_buffer += event.unicode.upper()

    # 2. Rendering
    screen.fill(BG_COLOR)

    # Draw Dual Engine Panels
    panel1_rect = pygame.Rect(15, 15, 425, 360)
    panel2_rect = pygame.Rect(460, 15, 425, 360)
    
    draw_engine_panel(screen, panel1_rect, 1, engines[1], is_focused=(active_num_engine == 1))
    draw_engine_panel(screen, panel2_rect, 2, engines[2], is_focused=(active_num_engine == 2))
    
    draw_hud(screen)

    pygame.display.flip()
    clock.tick(60)

pygame.quit()
sys.exit()