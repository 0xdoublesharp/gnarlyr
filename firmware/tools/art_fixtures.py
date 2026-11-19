import math
import random

from PIL import Image


def photo(width, height, seed=7):
    rng = random.Random(seed)
    image = Image.new("RGB", (width, height))
    image.putdata([(max(0, min(255, 40 + x * 160 // width + rng.randrange(-20, 21))),
                    max(0, min(255, 30 + y * 150 // height + rng.randrange(-20, 21))),
                    max(0, min(255, 90 + (x + y) * 100 // (width + height) + rng.randrange(-20, 21))))
                   for y in range(height) for x in range(width)])
    return image


def flat(width, height):
    image = Image.new("RGB", (width, height))
    horizon = height * 3 // 5
    pixels = []
    for y in range(height):
        for x in range(width):
            if y < horizon:
                band = y * 12 // horizon
                colour = (255 - band * 12, 60 + band * 10, 120 + band * 8)
                if math.hypot(x - width / 2, y - horizon) < height / 4 and (y // 4) % 3:
                    colour = (255, 200 - band * 5, 80)
            else:
                colour = (30, 10, 60) if (x % 24 and (y - horizon) % 10) else (255, 40, 200)
            pixels.append(colour)
    image.putdata(pixels)
    return image


def overlay(width, height):
    image = Image.new("RGBA", (width, height))
    pixels = []
    for y in range(height):
        for x in range(width):
            edge = min(x, y, width - 1 - x, height - 1 - y)
            if edge < 6:
                pixels.append((200, 220, 255, 255 - edge * 40))
            elif width // 3 < x < width // 2 and height // 3 < y < height // 2:
                pixels.append((255, 80, 40, 128))
            else:
                pixels.append((0, 0, 0, 0))
    image.putdata(pixels)
    return image


def noise(width, height, seed=3):
    rng = random.Random(seed)
    image = Image.new("RGB", (width, height))
    image.putdata([(rng.randrange(256), rng.randrange(256), rng.randrange(256))
                   for _ in range(width * height)])
    return image


def ui(width, height):
    image = Image.new("RGB", (width, height), (16, 20, 28))
    pixels = image.load()
    for y in range(height):
        for x in range(width):
            if y < 30:
                pixels[x, y] = (40, 52, 70)
            elif (y - 30) % 34 < 2:
                pixels[x, y] = (60, 64, 80)
            elif 120 < y < 154:
                pixels[x, y] = (70, 120, 200)
            if 14 < (y - 30) % 34 < 22 and 40 < x < 40 + (y * 37 % 300) and (x // 3) % 4:
                pixels[x, y] = (230, 230, 240)
    return image


def pixel_art(width, height):
    colours = ((16, 24, 20), (40, 71, 52), (146, 228, 94), (232, 199, 108), (219, 245, 216),
               (178, 212, 173))
    image = Image.new("RGB", (width, height))
    image.putdata([colours[((x // 12) * 7 + (y // 12) * 3 + (x // 48) * (y // 37)) % 6]
                   for y in range(height) for x in range(width)])
    return image


FIXTURES = {"photo": photo, "flat": flat, "ui": ui, "pixel": pixel_art, "overlay": overlay}
