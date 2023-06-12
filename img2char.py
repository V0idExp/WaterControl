import click
from PIL import Image


@click.command()
@click.argument('image', type=click.Path(exists=True, dir_okay=False, readable=True))
def img2char(image):
    """Convert a 5x8 image to a byte array, compatible with LED displays."""
    img = Image.open(image)
    if img.size != (5, 8):
        raise ValueError(f'unsupported image size {img.size}')

    bmp = img.convert('1').getdata()
    data = bytearray()
    for y in range(8):
        line = 0
        for x in range(5):
            val = bmp[y * 5 + x]
            line |= int(not bool(val)) << (4 - x)
        data.append(line)

    print(', '.join(hex(b) for b in data))


if __name__ == '__main__':
    try:
        img2char()
    except Exception as err:
        print(f'error: {err}')
