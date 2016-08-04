class PingPongAlongX
{
	GameObject @m_gameobject;
	Transform @m_transform;
	
	//= MISC ===============
	Vector3 m_currentPos;
	float m_speed = 1.0f;
	float m_distance = 4.0f;
	//======================
	
	// Constructor
	PingPongAlongX(GameObject @obj)
	{
		@m_gameobject = obj;
	}
	
	// Use this for initialization
	void Start()
	{
		@m_transform = m_gameobject.GetTransform();	
		m_currentPos = m_transform.GetPositionLocal();
	}

	// Update is called once per frame
	void Update()
	{	
		m_currentPos += Vector3(m_speed * time.GetDeltaTime(), 0.0f , 0.0f);
		m_transform.SetPositionLocal(m_currentPos);

		if (m_currentPos.x >= m_distance || m_currentPos.x <= -m_distance)
			m_speed *= -1;
	}
}