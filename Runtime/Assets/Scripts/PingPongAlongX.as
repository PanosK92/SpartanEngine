class PingPongAlongX
{
	GameObject @m_gameobject;
	Transform @m_transform;
	
	//= MISC ===================
	Vector3 m_currentPos;
	Quaternion m_currentRot;
	float m_speed = 0.1f;
	float m_distance = 0.0f;
	float m_maxDistance = 4.0f;
	//==========================
	
	// Constructor
	PingPongAlongX(GameObject @obj)
	{
		@m_gameobject = obj;
		@m_transform = m_gameobject.GetTransform();	
	}
	
	// Use this for initialization
	void Start()	
	{
		m_currentPos = m_transform.GetPositionLocal();
		m_currentRot = m_transform.GetRotationLocal();
		m_distance = m_maxDistance * 0.5f;
	}

	// Update is called once per frame
	void Update()
	{	
		float speed =  m_speed * time.GetDeltaTime();
		
		m_currentPos += m_transform.GetForward() * speed;	
		m_distance += speed;
		
		if (m_distance > m_maxDistance || m_distance < -m_maxDistance)
		{
			m_speed *= -1;
		}
		
		m_transform.SetPositionLocal(m_currentPos);

		
	}
}