class PingPongAlongX
{
	GameObject @m_gameobject;
	Transform @m_transform;
	
	//= MISC ===============
	float m_currentPos;
	Vector3 m_currentRot;
	float m_speed = 4.5f;
	float m_distance = 11.0f;
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
		m_currentPos = m_transform.GetPositionLocal().x;
		m_currentRot = Vector3(0, 90, 0);
	}

	// Update is called once per frame
	void Update()
	{	
		m_currentPos += m_speed * time.GetDeltaTime();
		
		if (m_currentPos > m_distance || m_currentPos < -m_distance)
		{
			if (m_currentPos > m_distance)
				m_currentPos = m_distance;
			else if (m_currentPos < -m_distance)
				m_currentPos = -m_distance;
				
			m_speed *= -1;
			m_currentRot = m_currentRot * (-1);
			
			m_transform.SetRotationLocal(QuaternionFromEuler(m_currentRot));
		}		
		
		if (m_speed < 0)
			m_currentRot.y = Lerp(m_currentRot.y, -90, 0.0002f * time.GetDeltaTime());
		else
			m_currentRot.y = Lerp(m_currentRot.y, 90, 0.002f * time.GetDeltaTime());
			
		m_transform.SetPositionLocal(Vector3(m_currentPos, 0, 0));
		
	}
}