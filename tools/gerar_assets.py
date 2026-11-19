#!/usr/bin/env python3
"""Gera os PNGs transparentes do jogo em assets/.

Todas as imagens sao desenhadas aqui, sem material de terceiros. O resultado e
deterministico. Uso: python3 tools/gerar_assets.py  (requer Pillow)
"""
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

ESCALA = 4
TAMANHO = 128
BASE = TAMANHO * ESCALA
SAIDA = Path(__file__).resolve().parent.parent / "assets"

CONTORNO = (45, 30, 22, 255)
LARANJA = (232, 120, 40, 255)
TEAL = (30, 130, 135, 255)
BRANCO = (250, 247, 238, 255)
OURO = (246, 190, 50, 255)
OURO_CLARO = (255, 226, 120, 255)
OURO_ESCURO = (178, 118, 22, 255)


def tela():
    imagem = Image.new("RGBA", (BASE, BASE), (0, 0, 0, 0))
    return imagem, ImageDraw.Draw(imagem)


def p(*valores):
    """Converte coordenadas da grade 128x128 para a tela ampliada."""
    return [v * ESCALA for v in valores]


def pontos(lista):
    return [(x * ESCALA, y * ESCALA) for x, y in lista]


def elipse(d, cx, cy, rx, ry, cor, borda=None, largura=3):
    caixa = p(cx - rx, cy - ry, cx + rx, cy + ry)
    d.ellipse(caixa, fill=cor, outline=borda, width=largura * ESCALA if borda else 0)


def linha(d, lista, cor, largura=3):
    d.line(pontos(lista), fill=cor, width=largura * ESCALA, joint="curve")
    raio = largura / 2
    for x, y in (lista[0], lista[-1]):
        elipse(d, x, y, raio, raio, cor)


def poligono(d, lista, cor, borda=CONTORNO, largura=3):
    d.polygon(pontos(lista), fill=cor)
    if borda:
        linha(d, lista + [lista[0]], borda, largura)


def arco(cx, cy, raio, inicio, fim, passos=40):
    angulos = [math.radians(inicio + (fim - inicio) * i / passos) for i in range(passos + 1)]
    return [(cx + raio * math.cos(a), cy + raio * math.sin(a)) for a in angulos]


def estrela(cx, cy, externo, interno, pontas=5):
    lista = []
    for i in range(pontas * 2):
        raio = externo if i % 2 == 0 else interno
        angulo = -math.pi / 2 + i * math.pi / pontas
        lista.append((cx + raio * math.cos(angulo), cy + raio * math.sin(angulo)))
    return lista


def salvar(imagem, nome):
    final = imagem.resize((TAMANHO, TAMANHO), Image.LANCZOS)
    final.save(SAIDA / f"{nome}.png", optimize=True)


def forma_castanha(d, dx=0, dy=0, fator=1.0, cor=(150, 112, 74, 255), borda=CONTORNO):
    """Castanha em forma de rim, usada no caju e na moeda."""
    lista = []
    for i in range(48):
        t = i * math.pi / 24
        x = 28 * math.cos(t)
        y = 13 * math.sin(t) + 7 - 15 * math.cos(t) ** 2
        lista.append((64 + x * fator + dx, 76 + y * fator + dy))
    poligono(d, lista, cor, borda, 3)


def caju():
    rascunho, d = tela()
    d.polygon(pontos([(40, 10), (88, 10), (104, 30), (92, 58), (76, 84), (52, 84), (36, 58), (24, 30)]),
              fill=(232, 72, 44, 255))
    suave = rascunho.filter(ImageFilter.GaussianBlur(5 * ESCALA))
    mascara = suave.getchannel("A").point(lambda v: 255 if v > 118 else 0)
    borda = mascara.filter(ImageFilter.MaxFilter(11))
    imagem = Image.new("RGBA", (BASE, BASE), (0, 0, 0, 0))
    imagem.paste(CONTORNO, (0, 0), borda)
    imagem.paste((238, 82, 44, 255), (0, 0), mascara)
    d = ImageDraw.Draw(imagem)
    elipse(d, 46, 32, 10, 14, (255, 150, 70, 255))
    elipse(d, 43, 26, 4, 7, (255, 226, 160, 255))
    linha(d, [(64, 12), (66, 3)], (86, 120, 54, 255), 5)
    forma_castanha(d, dy=30, fator=0.95)
    salvar(imagem, "caju")


def moeda():
    imagem, d = tela()
    elipse(d, 64, 64, 58, 58, OURO_ESCURO, CONTORNO, 4)
    elipse(d, 62, 61, 50, 50, OURO)
    elipse(d, 62, 61, 40, 40, OURO_CLARO, OURO_ESCURO, 3)
    forma_castanha(d, dx=-2, dy=-12, fator=0.9, cor=OURO, borda=OURO_ESCURO)
    linha(d, arco(62, 61, 46, 200, 262, 14), (255, 246, 205, 255), 4)
    salvar(imagem, "moeda")


def ninho():
    imagem, d = tela()
    for cx, cy, cor in ((46, 56, (240, 232, 214, 255)), (82, 56, (226, 214, 190, 255)), (64, 50, BRANCO)):
        elipse(d, cx, cy, 16, 21, cor, CONTORNO, 3)
    poligono(d, arco(64, 56, 56, 0, 180, 40), (136, 90, 48, 255), CONTORNO, 4)
    tons = [(176, 124, 68, 255), (104, 66, 34, 255), (200, 150, 88, 255)]
    for i in range(16):
        y = 64 + (i % 5) * 9
        meia = math.sqrt(max(56 ** 2 - (y - 56) ** 2, 0)) - 7
        x0 = 64 - meia + (i * 13) % 18
        x1 = min(64 + meia, x0 + 44)
        linha(d, [(x0, y + (i % 3) * 2), ((x0 + x1) / 2, y - 5), (x1, y + (i % 2) * 3)], tons[i % 3], 4)
    linha(d, [(9, 58), (40, 64), (70, 60), (119, 58)], (200, 150, 88, 255), 6)
    salvar(imagem, "ninho")


def seta_bussola():
    imagem, d = tela()
    poligono(d, [(64, 6), (106, 112), (64, 90)], (214, 58, 44, 255), CONTORNO, 5)
    poligono(d, [(64, 6), (22, 112), (64, 90)], (250, 104, 70, 255), CONTORNO, 5)
    linha(d, [(64, 14), (64, 86)], (255, 222, 190, 255), 3)
    salvar(imagem, "seta_bussola")


def selo_record():
    imagem, d = tela()
    poligono(d, [(38, 72), (20, 122), (44, 110), (58, 124), (66, 80)], (196, 44, 56, 255))
    poligono(d, [(90, 72), (108, 122), (84, 110), (70, 124), (62, 80)], (222, 62, 72, 255))
    dentes = []
    for i in range(32):
        raio = 54 if i % 2 == 0 else 47
        angulo = i * math.pi / 16
        dentes.append((64 + raio * math.cos(angulo), 56 + raio * math.sin(angulo)))
    poligono(d, dentes, OURO, CONTORNO, 4)
    elipse(d, 64, 56, 38, 38, OURO_CLARO, OURO_ESCURO, 3)
    poligono(d, estrela(64, 56, 30, 13), (232, 92, 40, 255), CONTORNO, 3)
    salvar(imagem, "selo_record")


def fundo_icone(d, cor):
    elipse(d, 64, 64, 59, 59, cor, CONTORNO, 4)
    elipse(d, 64, 64, 50, 50, None, (255, 255, 255, 70), 2)


def icone_velocidade(d):
    poligono(d, [(72, 18), (36, 72), (60, 72), (50, 110), (92, 52), (66, 52)], OURO_CLARO)


def icone_agilidade(d):
    trajeto = arco(62, 66, 32, 150, 395, 40)
    linha(d, trajeto, CONTORNO, 15)
    linha(d, trajeto, BRANCO, 8)
    x, y = trajeto[-1]
    poligono(d, [(x + 19, y - 6), (x - 15, y - 11), (x - 2, y + 22)], OURO_CLARO)


def icone_carga(d):
    poligono(d, [(40, 50), (88, 50), (100, 100), (28, 100)], (198, 142, 84, 255))
    linha(d, arco(64, 50, 24, 180, 360, 20), CONTORNO, 6)
    forma_castanha(d, dy=-2, fator=0.62, cor=OURO_CLARO)


def icone_alcance(d):
    elipse(d, 64, 88, 11, 11, OURO_CLARO, CONTORNO, 3)
    for raio in (26, 42, 58):
        linha(d, arco(64, 88, raio, 228, 312, 18), CONTORNO, 11)
        linha(d, arco(64, 88, raio, 228, 312, 18), BRANCO, 5)


def icone_defesa(d):
    escudo = [(64, 18), (98, 30), (98, 62), (90, 84), (64, 110), (38, 84), (30, 62), (30, 30)]
    poligono(d, escudo, (214, 228, 236, 255))
    poligono(d, [(64, 26), (90, 36), (90, 62), (83, 80), (64, 99)], (120, 170, 196, 255), None)


def icone_alarme(d):
    sino = arco(64, 54, 25, 180, 360, 24) + [(89, 80), (100, 92), (28, 92), (39, 80)]
    elipse(d, 64, 101, 10, 10, OURO, CONTORNO, 3)
    poligono(d, sino, OURO_CLARO)
    elipse(d, 64, 25, 6, 6, OURO, CONTORNO, 3)


def icone_cofre(d):
    for x in (38, 90):
        linha(d, [(x, 98), (x, 106)], CONTORNO, 7)
    poligono(d, [(26, 28), (102, 28), (102, 98), (26, 98)], (92, 108, 120, 255))
    poligono(d, [(36, 38), (92, 38), (92, 88), (36, 88)], (152, 168, 178, 255))
    elipse(d, 64, 63, 18, 18, OURO_CLARO, CONTORNO, 4)
    linha(d, [(64, 50), (64, 76)], CONTORNO, 4)
    linha(d, [(51, 63), (77, 63)], CONTORNO, 4)


def icone_tamanho(d):
    poligono(d, [(46, 46), (82, 46), (82, 82), (46, 82)], (198, 142, 84, 255))
    for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
        ponta = (64 + sx * 40, 64 + sy * 40)
        linha(d, [(64 + sx * 14, 64 + sy * 14), ponta], CONTORNO, 11)
        linha(d, [(64 + sx * 14, 64 + sy * 14), ponta], BRANCO, 5)
        poligono(d, [ponta, (ponta[0] - sx * 20, ponta[1]), (ponta[0], ponta[1] - sy * 20)], OURO_CLARO)


UPGRADES = {
    "upgrade_velocidade": (LARANJA, icone_velocidade),
    "upgrade_agilidade": (LARANJA, icone_agilidade),
    "upgrade_carga": (LARANJA, icone_carga),
    "upgrade_alcance": (LARANJA, icone_alcance),
    "upgrade_defesa": (TEAL, icone_defesa),
    "upgrade_alarme": (TEAL, icone_alarme),
    "upgrade_cofre": (TEAL, icone_cofre),
    "upgrade_tamanho": (TEAL, icone_tamanho),
}


def main():
    SAIDA.mkdir(exist_ok=True)
    caju()
    moeda()
    ninho()
    seta_bussola()
    selo_record()
    for nome, (cor, desenho) in UPGRADES.items():
        imagem, d = tela()
        fundo_icone(d, cor)
        desenho(d)
        salvar(imagem, nome)


if __name__ == "__main__":
    main()
