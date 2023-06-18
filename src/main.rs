use std::{fs::{self, File}, io::{Cursor, BufRead, Read, Write}, num};

use byteorder::{ReadBytesExt, LE, BE};

mod pmx;
mod ktmdl;

fn read_string_to_null<T>(reader: &mut T) -> String 
    where T: BufRead {
        let mut buf = Vec::new();
        loop {
            let c = reader.read_u8().unwrap();
            if c == 0 {
                break;
            }
            buf.push(c);
        }
        String::from_utf8(buf).unwrap()
}

#[derive(Debug)]
struct Info {
    name_offset: u32,
    offset: u32,
    size: u32,
    zsize: u32,
}

fn uncompresse(data: &Vec<u8>, raw_size: u32) -> Vec<u8> {
    let length = data.len() as u64;
    let mut reader = Cursor::new(data);
    let mut control_code: u16 = 0;
    let mut output: Vec<u8> = Vec::with_capacity(raw_size as _);
    while length - reader.position() > 0 {
        if control_code & 0x100 == 0 {
            control_code = reader.read_u8().unwrap() as u16;
            control_code |= 0xFF00;
        }
        if control_code & 1 != 0 {
            output.push(reader.read_u8().unwrap());
        } else {
            let flag = reader.read_u16::<BE>().unwrap();
            if flag == 0 {
                break;
            }
            let offset = flag >> 4;
            let len = (flag & 0xF) + 3;
            for _ in 0..len {
                let pos = output.len() as i32 - offset as i32;
                if pos < 0 {
                    output.push(0);
                } else {
                    output.push(output[pos as usize]);
                }
            }
        }
        control_code >>= 1;
    }
    assert_eq!(output.len(), raw_size as usize);
    output
}

fn main() {
    let content = fs::read("model_pl_unaf000.arc").unwrap();
    let mut reader = Cursor::new(content);
    let _magic = reader.read_u32::<LE>().unwrap();
    let _version = reader.read_u32::<LE>().unwrap();
    let files = reader.read_u32::<LE>().unwrap();
    let _ = reader.read_u32::<LE>().unwrap();
    let mut infos = Vec::new();
    for _ in 0..files {
        infos.push(Info {
            name_offset: reader.read_u32::<LE>().unwrap(),
            offset: reader.read_u32::<LE>().unwrap(),
            size: reader.read_u32::<LE>().unwrap(),
            zsize: reader.read_u32::<LE>().unwrap(),
        });
    }
    let mut model: Vec<u8> = Vec::new();
    let mut b2it: Vec<String> = Vec::new();
    let mut save_path = String::new();
    for info in &infos {
        reader.set_position(info.name_offset as _);
        let name = read_string_to_null(&mut reader);
        eprintln!("{}: {:?}", name, info);

        reader.set_position(info.offset as _);
        let mut compressed = vec![0u8; info.zsize as _];
        reader.read_exact(&mut compressed).unwrap();
        let uncompressed = if info.size == info.zsize {
            compressed
        } else {
            uncompresse(&compressed, info.size)
        };
        let path = std::path::Path::new(&name);
        let dir_path = path.parent().unwrap();
        std::fs::create_dir_all(dir_path).unwrap();
        std::fs::write(&name, &uncompressed).unwrap();
        if name.ends_with(".b2it") {
            assert_eq!(b2it.len(), 0);
            b2it = ktmdl::parse_b2it(&uncompressed);
        } else if name.ends_with(".model") {
            assert_eq!(model.len(), 0);
            model = uncompressed.clone();
            save_path = name;
        }
    }
    ktmdl::ktmodel_to_pmx(model, b2it, &save_path)

}

