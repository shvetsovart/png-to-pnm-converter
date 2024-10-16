from PIL import Image

frames = []

for frame_number in range(1, 20):
    frame = Image.open(f'dir/{frame_number}.pnm')
    frames.append(frame)

frames[0].save(
    'homer.gif',
    save_all=True,
    append_images=frames[1:],
    optimize=True,
    duration=200,
    loop=0
)