use serde::{Deserialize, Serialize};
use std::fmt;
use tokio::io::{AsyncRead, AsyncReadExt, AsyncWrite, AsyncWriteExt};

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
pub enum FrameError {
    Io(std::io::Error),
    Json(serde_json::Error),
    TooLarge(u32),
    UnknownType(u8),
}

impl fmt::Display for FrameError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            FrameError::Io(e) => write!(f, "io: {}", e),
            FrameError::Json(e) => write!(f, "json: {}", e),
            FrameError::TooLarge(n) => write!(f, "frame too large: {} bytes", n),
            FrameError::UnknownType(t) => write!(f, "unknown frame type: 0x{:02x}", t),
        }
    }
}

impl std::error::Error for FrameError {}

impl From<std::io::Error> for FrameError {
    fn from(e: std::io::Error) -> Self {
        FrameError::Io(e)
    }
}

impl From<serde_json::Error> for FrameError {
    fn from(e: serde_json::Error) -> Self {
        FrameError::Json(e)
    }
}

pub async fn write_frame<W>(w: &mut W, frame: &Frame) -> Result<(), FrameError>
where
    W: AsyncWrite + Unpin,
{
    let (typ, payload) = match frame {
        Frame::Hello(p) => (FRAME_HELLO, serde_json::to_vec(p)?),
        Frame::HelloAck(p) => (FRAME_HELLO_ACK, serde_json::to_vec(p)?),
        Frame::Event(p) => (FRAME_EVENT, serde_json::to_vec(p)?),
        Frame::Bye(p) => (FRAME_BYE, serde_json::to_vec(p)?),
        Frame::Ping => (FRAME_PING, Vec::new()),
        Frame::Pong => (FRAME_PONG, Vec::new()),
        Frame::Error(p) => (FRAME_ERROR, serde_json::to_vec(p)?),
    };
    let total = payload.len() as u32 + 1;
    if total > MAX_FRAME_BODY + 1 {
        return Err(FrameError::TooLarge(total));
    }
    w.write_all(&total.to_le_bytes()).await?;
    w.write_all(&[typ]).await?;
    if !payload.is_empty() {
        w.write_all(&payload).await?;
    }
    w.flush().await?;
    Ok(())
}

pub async fn read_frame<R>(r: &mut R) -> Result<Frame, FrameError>
where
    R: AsyncRead + Unpin,
{
    let mut len_buf = [0u8; 4];
    r.read_exact(&mut len_buf).await?;
    let total = u32::from_le_bytes(len_buf);
    if total == 0 || total > MAX_FRAME_BODY + 1 {
        return Err(FrameError::TooLarge(total));
    }
    let mut typ_buf = [0u8; 1];
    r.read_exact(&mut typ_buf).await?;
    let payload_len = (total - 1) as usize;
    let mut payload = vec![0u8; payload_len];
    if payload_len > 0 {
        r.read_exact(&mut payload).await?;
    }
    let frame = match typ_buf[0] {
        FRAME_HELLO => Frame::Hello(serde_json::from_slice(&payload)?),
        FRAME_HELLO_ACK => Frame::HelloAck(serde_json::from_slice(&payload)?),
        FRAME_EVENT => Frame::Event(serde_json::from_slice(&payload)?),
        FRAME_BYE => Frame::Bye(serde_json::from_slice(&payload)?),
        FRAME_PING => Frame::Ping,
        FRAME_PONG => Frame::Pong,
        FRAME_ERROR => Frame::Error(serde_json::from_slice(&payload)?),
        other => return Err(FrameError::UnknownType(other)),
    };
    Ok(frame)
}

#[cfg(test)]
mod tests {
    use super::*;
    use tokio::io::duplex;

    async fn round_trip(frame: Frame) -> Frame {
        let (mut a, mut b) = duplex(64 * 1024);
        write_frame(&mut a, &frame).await.unwrap();
        read_frame(&mut b).await.unwrap()
    }

    #[tokio::test]
    async fn round_trip_hello() {
        let hello = Frame::Hello(HelloPayload {
            room_id: "room-001".into(),
            player_index: 0,
            name: "tester".into(),
            version: "0.1".into(),
        });
        assert_eq!(round_trip(hello.clone()).await, hello);
    }

    #[tokio::test]
    async fn round_trip_hello_ack() {
        let ack = Frame::HelloAck(HelloAckPayload {
            ok: true,
            errnu: 0,
            message: String::new(),
        });
        assert_eq!(round_trip(ack.clone()).await, ack);
    }

    #[tokio::test]
    async fn round_trip_event() {
        let ev = Frame::Event(EventPayload {
            dir: "c2s".into(),
            ename: "buy_tower".into(),
            evalue: r#"{"id":1}"#.into(),
            ts: 1.5,
            player_index: 0,
            room_id: "room-001".into(),
        });
        assert_eq!(round_trip(ev.clone()).await, ev);
    }

    #[tokio::test]
    async fn round_trip_bye() {
        let bye = Frame::Bye(ByePayload {
            reason: "ok".into(),
        });
        assert_eq!(round_trip(bye.clone()).await, bye);
    }

    #[tokio::test]
    async fn round_trip_ping_pong() {
        assert_eq!(round_trip(Frame::Ping).await, Frame::Ping);
        assert_eq!(round_trip(Frame::Pong).await, Frame::Pong);
    }

    #[tokio::test]
    async fn round_trip_error() {
        let err = Frame::Error(ErrorPayload {
            errnu: 2,
            message: "no room".into(),
        });
        assert_eq!(round_trip(err.clone()).await, err);
    }

    #[tokio::test]
    async fn unknown_type_byte() {
        let (mut a, mut b) = duplex(64);
        a.write_all(&1u32.to_le_bytes()).await.unwrap();
        a.write_all(&[0xAA]).await.unwrap();
        a.flush().await.unwrap();
        match read_frame(&mut b).await {
            Err(FrameError::UnknownType(0xAA)) => {}
            other => panic!("expected UnknownType, got {:?}", other),
        }
    }

    #[tokio::test]
    async fn too_large_length() {
        let (mut a, mut b) = duplex(64);
        let bad = (MAX_FRAME_BODY + 2).to_le_bytes();
        a.write_all(&bad).await.unwrap();
        a.flush().await.unwrap();
        match read_frame(&mut b).await {
            Err(FrameError::TooLarge(_)) => {}
            other => panic!("expected TooLarge, got {:?}", other),
        }
    }

    #[tokio::test]
    async fn zero_length_rejected() {
        let (mut a, mut b) = duplex(64);
        a.write_all(&0u32.to_le_bytes()).await.unwrap();
        a.flush().await.unwrap();
        match read_frame(&mut b).await {
            Err(FrameError::TooLarge(0)) => {}
            other => panic!("expected TooLarge(0), got {:?}", other),
        }
    }

    #[tokio::test]
    async fn truncated_frame_errors() {
        let (mut a, mut b) = duplex(64);
        a.write_all(&10u32.to_le_bytes()).await.unwrap();
        a.write_all(&[FRAME_EVENT]).await.unwrap();
        a.write_all(b"abc").await.unwrap();
        drop(a);
        match read_frame(&mut b).await {
            Err(FrameError::Io(_)) => {}
            other => panic!("expected Io error, got {:?}", other),
        }
    }
}
