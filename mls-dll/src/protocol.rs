use serde::{Deserialize, Serialize};

pub const FRAME_HELLO: u8 = 0x01;
pub const FRAME_HELLO_ACK: u8 = 0x02;
pub const FRAME_EVENT: u8 = 0x03;
pub const FRAME_BYE: u8 = 0x04;
pub const FRAME_PING: u8 = 0x05;
pub const FRAME_PONG: u8 = 0x06;
pub const FRAME_ERROR: u8 = 0x0F;

pub const MAX_FRAME_BODY: u32 = 64 * 1024;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct HelloPayload {
    pub room_id: String,
    #[serde(default)]
    pub player_index: i32,
    #[serde(default)]
    pub name: String,
    #[serde(default)]
    pub version: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct HelloAckPayload {
    pub ok: bool,
    pub errnu: i32,
    #[serde(default)]
    pub message: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct EventPayload {
    pub dir: String,
    pub ename: String,
    #[serde(default)]
    pub evalue: String,
    #[serde(default)]
    pub ts: f64,
    #[serde(default)]
    pub player_index: i32,
    #[serde(default)]
    pub room_id: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct ByePayload {
    #[serde(default)]
    pub reason: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct ErrorPayload {
    pub errnu: i32,
    #[serde(default)]
    pub message: String,
}

#[derive(Debug, Clone, PartialEq)]
pub enum Frame {
    Hello(HelloPayload),
    HelloAck(HelloAckPayload),
    Event(EventPayload),
    Bye(ByePayload),
    Ping,
    Pong,
    Error(ErrorPayload),
}

#[derive(Debug)]
#[allow(dead_code)] // fields exist only for Debug formatting in error logs
pub enum FrameError {
    Json(serde_json::Error),
    TooLarge(u32),
    UnknownType(u8),
}

pub fn encode_frame(frame: &Frame) -> Result<Vec<u8>, FrameError> {
    let (typ, payload): (u8, Vec<u8>) = match frame {
        Frame::Hello(p) => (FRAME_HELLO, serde_json::to_vec(p).map_err(FrameError::Json)?),
        Frame::HelloAck(p) => (FRAME_HELLO_ACK, serde_json::to_vec(p).map_err(FrameError::Json)?),
        Frame::Event(p) => (FRAME_EVENT, serde_json::to_vec(p).map_err(FrameError::Json)?),
        Frame::Bye(p) => (FRAME_BYE, serde_json::to_vec(p).map_err(FrameError::Json)?),
        Frame::Ping => (FRAME_PING, Vec::new()),
        Frame::Pong => (FRAME_PONG, Vec::new()),
        Frame::Error(p) => (FRAME_ERROR, serde_json::to_vec(p).map_err(FrameError::Json)?),
    };
    let total = payload.len() as u32 + 1;
    if total > MAX_FRAME_BODY + 1 {
        return Err(FrameError::TooLarge(total));
    }
    let mut out = Vec::with_capacity(4 + 1 + payload.len());
    out.extend_from_slice(&total.to_le_bytes());
    out.push(typ);
    out.extend_from_slice(&payload);
    Ok(out)
}

pub fn decode_frame(typ: u8, body: &[u8]) -> Result<Frame, FrameError> {
    let frame = match typ {
        FRAME_HELLO => Frame::Hello(serde_json::from_slice(body).map_err(FrameError::Json)?),
        FRAME_HELLO_ACK => Frame::HelloAck(serde_json::from_slice(body).map_err(FrameError::Json)?),
        FRAME_EVENT => Frame::Event(serde_json::from_slice(body).map_err(FrameError::Json)?),
        FRAME_BYE => Frame::Bye(serde_json::from_slice(body).map_err(FrameError::Json)?),
        FRAME_PING => Frame::Ping,
        FRAME_PONG => Frame::Pong,
        FRAME_ERROR => Frame::Error(serde_json::from_slice(body).map_err(FrameError::Json)?),
        other => return Err(FrameError::UnknownType(other)),
    };
    Ok(frame)
}
